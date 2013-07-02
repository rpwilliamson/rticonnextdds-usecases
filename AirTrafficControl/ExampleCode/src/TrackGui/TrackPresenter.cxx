/*********************************************************************************************
(c) 2005-2013 Copyright, Real-Time Innovations, Inc.  All rights reserved.    	                             
RTI grants Licensee a license to use, modify, compile, and create derivative works 
of the Software.  Licensee has the right to distribute object form only for use with RTI 
products.  The Software is provided �as is�, with no warranty of any type, including 
any warranty for fitness for any purpose. RTI is under no obligation to maintain or 
support the Software.  RTI shall not be liable for any incidental or consequential 
damages arising out of the use or inability to use the software.
**********************************************************************************************/
#include "TrackPresenter.h"
#include "TrackApp.h"
#include "TrackGUI.h"

#include "../CommonInfrastructure/OSAPI.h"
#include <vector>
#include <sstream>
using namespace std;
using namespace DDS;
using namespace com::rti::atc::generated;

// Create a new network receiver class.  This class is the glue between
// the UI and the data source - in this case, the DDS network data
// that is arriving asynchronously.
FlightInfoNetworkReceiver::FlightInfoNetworkReceiver(TrackApp *parent)
	: _shuttingDown(false), 
		_app(parent)
{
	_mutex = new OSMutex();

}

FlightInfoNetworkReceiver::~FlightInfoNetworkReceiver()
{
	delete _mutex;
}

// Observer pattern for notifications. The track presenter will notify various
// UI listeners that a track has been deleted, and the UI listeners can redraw
// the UI as necessary.  This allows a single update to notify multiple windows
// or panels that a track has been deleted.
void FlightInfoNetworkReceiver::NotifyListenersDeleteTrack(
	const std::vector<FlightInfo *>flights)
{
	_mutex->Lock();
	for (std::vector<FlightInfoListener *>::iterator it = _listeners.begin(); 
		it != _listeners.end(); ++it) {
		if (false == (*it)->TrackDelete(flights)) {
			std::stringstream errss;
			errss << "NotifyListenersDeleteTrack(): error deleting track.";
			_mutex->Unlock();
			throw errss.str();
		}
	}

	_mutex->Unlock();
}

// Observer pattern for notifications. The track presenter will notify various
// UI listeners that a track has been updated, and the UI listeners can redraw
// the UI as necessary.  This allows a single update to notify multiple windows
// or panels that a track has been updated.
void FlightInfoNetworkReceiver::NotifyListenersUpdateTrack(
	const std::vector<FlightInfo *> flights)
{
	_mutex->Lock();
	for (std::vector<FlightInfoListener *>::iterator it = 
		_listeners.begin(); it != _listeners.end(); ++it) {

		if (false == (*it)->TrackUpdate(flights)) {
			std::stringstream errss;
			errss << "NotifyListenersUpdateTrack(): error updating track.";
			_mutex->Unlock();
			throw errss.str();

		}
	}
	_mutex->Unlock();
}

// Remove listeners.  This must be called before shutdown, because the frames that
// are updating may be deleted before the data source.  So, the listeners must be
// removed from the UI before it shuts down.
void FlightInfoNetworkReceiver::RemoveListener(
	const FlightInfoListener *listener)
{
	std::vector<FlightInfoListener *>::iterator toErase;

	_mutex->Lock();

	for(std::vector<FlightInfoListener *>::iterator it = 
		_listeners.begin(); it != _listeners.end(); 
		++it)
	{
		if ((*it) == listener)
		{
			toErase = it;
		}
	}

	_listeners.erase(toErase);
	_mutex->Unlock();

}


// This starts receiving the network data - or, more accurately, this starts
// the process of accessing data that is already being received asynchronously
// over the network as soon as the DDS discovery process is complete.
void FlightInfoNetworkReceiver::StartReceiving()
{
	OSThread *thread = new OSThread(
		(ThreadFunction)FlightInfoNetworkReceiver::ReceiveTracks, _app);	
	thread->Run();

	// When the thread is done, delete it
	delete thread;
}

// Update UI when track data changes.
void FlightInfoNetworkReceiver::ReceiveTracks(void *param)
{
	TrackApp *app = (TrackApp *)param;

	// Get access to the data reader objects that are receiving data over the 
	// network asynchronously.
	NetworkInterface *netInterface = app->GetNetworkInterface();
	TrackReader *reader = netInterface->GetTrackReader();
	FlightPlanReader *planReader = netInterface->GetFlightPlanReader();
	vector<Track *>tracks;
	vector <FlightInfo *>flights;

	// This periodically wakes up and updates the UI with the latest track
	// information that is available from the middleware, which has been 
	// collecting it asynchronously.
	DDS::Duration_t uiUpdatePeriod = {0,200000000};

	while (!app->ShuttingDown()) {

		// Note that this API allocates tracks that are a copy of tracks
		// in the queue.  This allows us to pass in an empty vector and
		// use the data in it however we want.
//		reader->WaitForTracks(&tracks);
		reader->GetCurrentTracks(&tracks);

		if (app->ShuttingDown())
		{
			return;
		}

		if (!tracks.empty()) 
		{

			for (unsigned int i = 0; i < tracks.size(); i++) {

				// Allocate a flight info object.  This will be filled in with
				// the flight plan information.
				FlightInfo *flightInfo = new FlightInfo;
				flightInfo->_track = tracks[i];

				flightInfo->_plan = FlightPlanTypeSupport::create_data();

				// This copies a flight plan from the middleware into 
				// the object passed in
				planReader->GetFlightPlan(tracks[i]->flightId, 
					flightInfo->_plan);
				flights.push_back(flightInfo);

				
			}

			// Notify the listeners that there is an upate to the track list
			app->GetPresenter()->NotifyListenersUpdateTrack(flights);

		}

		tracks.clear();

		// Delete the tracks we have just allocated.  There are more efficient
		// ways to do this by preallocating, but here we simply create and 
		// destroy the track information at each iteration.
		for (unsigned int i = 0; i < flights.size(); i++)
		{
			FlightPlanTypeSupport::delete_data(flights[i]->_plan);
			TrackTypeSupport::delete_data(flights[i]->_track);
			delete flights[i];
		}
		flights.clear();

		// Sleep until the next UI refresh
		NDDSUtility::sleep(uiUpdatePeriod);

	}


}
