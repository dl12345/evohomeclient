/*
 * Copyright (c) 2016 Gordon Bos <gordon@bosvangennip.nl> All rights reserved.
 *
 * Demo app for connecting to Evohome and Domoticz
 *
 *
 *
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <cstring>
#include <time.h>
#include "../domoticzclient/domoticzclient.h"
#include "../evohomeclient/evohomeclient.h"

#ifndef CONF_FILE
#define CONF_FILE "evoconfig"
#endif

#ifndef LOCKFILE
#define LOCKFILE "/tmp/evo-noup.tmp"
#endif

#ifndef SCHEDULE_CACHE
#define SCHEDULE_CACHE "schedules.json"
#endif

#define HARDWARE_TYPE "40"

#define CONTROLLER_SUBTYPE "Evohome"
#define CONTROLLER_SUBTYPE_ID "69"

#define HOTWATER_SUBTYPE "Hot Water"
#define HOTWATER_SUBTYPE_ID "71"

#define ZONE_SUBTYPE "Zone"
#define ZONE_SUBTYPE_ID "70"

using namespace std;

// Include common functions
#include "evo-common.cpp"



std::string backupfile;

time_t now;
int tzoffset=-1;

bool createdev = false;
bool updatedev = true;
bool reloadcache = false;



/*
 * Convert domoticz host settings into fully qualified url prefix
 */
std::string get_domoticz_host(std::string url, std::string port)
{
	stringstream ss;
	if (url.substr(0,4) != "http")
		ss << "http://";
	ss << url;
	if (port.length() > 0)
		ss << ":" << port;
	return ss.str();
}


void usage(std::string mode)
{
	if (mode == "badparm")
	{
		cout << "Bad parameter" << endl;
		exit(1);
	}
	if (mode == "short")
	{
		cout << "Usage: evo-setmode [-hv] [-c file] <evohome mode>" << endl;
		cout << "Type \"evo-setmode --help\" for more help" << endl;
		exit(0);
	}
	cout << "Usage: evo-setmode [OPTIONS] <evohome mode>" << endl;
	cout << endl;
	cout << "  -v, --verbose           print a lot of information" << endl;
	cout << "  -c, --conf=FILE         use FILE for server settings and credentials" << endl;
	cout << "  -h, --help              display this help and exit" << endl;
	exit(0);
}


void parse_args(int argc, char** argv) {
	int i=1;
	std::string word;
	while (i < argc) {
		word = argv[i];
		if (word.length() > 1 && word[0] == '-' && word[1] != '-') {
			for (size_t j=1;j<word.length();j++) {
				if (word[j] == 'h') {
					usage("short");
					exit(0);
				} else if (word[j] == 'i') {
					createdev = true;
				} else if (word[j] == 'v') {
					verbose = true;
				} else {
					usage("badparm");
					exit(1);
				}
			}
		} else if (word == "--help") {
			usage("long");
			exit(0);
		} else if (word == "--init") {
			createdev = true;
		} else if (word == "--verbose") {
			verbose = true;
		} else {
			usage("badparm");
			exit(1);
		}
		i++;
	}
}


std::string int_to_string(int myint)
{
	stringstream ss;
	ss << myint;
	return ss.str();
}


std::string utc_to_local(std::string utc_time)
{
	if (tzoffset == -1)
	{
		// calculate timezone offset once
		struct tm utime;
		gmtime_r(&now, &utime);
		tzoffset = difftime(mktime(&utime), now);
	}
	struct tm ltime;
	ltime.tm_isdst = -1;
	ltime.tm_year = atoi(utc_time.substr(0, 4).c_str()) - 1900;
	ltime.tm_mon = atoi(utc_time.substr(5, 2).c_str()) - 1;
	ltime.tm_mday = atoi(utc_time.substr(8, 2).c_str());
	ltime.tm_hour = atoi(utc_time.substr(11, 2).c_str());
	ltime.tm_min = atoi(utc_time.substr(14, 2).c_str());
	ltime.tm_sec = atoi(utc_time.substr(17, 2).c_str()) - tzoffset;
	time_t ntime = mktime(&ltime);
	ntime--; // prevent compiler warning
	char until[40];
	sprintf(until,"%04d-%02d-%02dT%02d:%02d:%02dZ",ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday,ltime.tm_hour,ltime.tm_min,ltime.tm_sec);
	return string(until);
}


map<std::string, std::string> evo_get_zone_data(EvohomeClient::temperatureControlSystem* tcs, int zoneindex)
{
	map<std::string, std::string> ret;

	ret["until"] = "";

	ret["zoneId"] = (*tcs->zones[zoneindex].status)["zoneId"].asString();
	ret["temperature"] = (*tcs->zones[zoneindex].status)["temperatureStatus"]["temperature"].asString();
	ret["targetTemperature"] = (*tcs->zones[zoneindex].status)["heatSetpointStatus"]["targetTemperature"].asString();
	ret["setpointMode"] = (*tcs->zones[zoneindex].status)["heatSetpointStatus"]["setpointMode"].asString();
	if (ret["setpointMode"] == "TemporaryOverride")
		ret["until"] = (*tcs->zones[zoneindex].status)["heatSetpointStatus"]["until"].asString();
	return ret;
}

std::string local_to_utc(std::string utc_time)
{
	if (tzoffset == -1)
	{
		// calculate timezone offset once
		struct tm utime;
		gmtime_r(&now, &utime);
		tzoffset = difftime(mktime(&utime), now);
	}
	struct tm ltime;
	ltime.tm_isdst = -1;
	ltime.tm_year = atoi(utc_time.substr(0, 4).c_str()) - 1900;
	ltime.tm_mon = atoi(utc_time.substr(5, 2).c_str()) - 1;
	ltime.tm_mday = atoi(utc_time.substr(8, 2).c_str());
	ltime.tm_hour = atoi(utc_time.substr(11, 2).c_str());
	ltime.tm_min = atoi(utc_time.substr(14, 2).c_str());
	ltime.tm_sec = atoi(utc_time.substr(17, 2).c_str()) + tzoffset;
	time_t ntime = mktime(&ltime);
	ntime--; // prevent compiler warning
	char until[40];
	sprintf(until,"%04d-%02d-%02dT%02d:%02d:%02dZ",ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday,ltime.tm_hour,ltime.tm_min,ltime.tm_sec);
	return string(until);
}


void update_zone(DomoticzClient dclient, map<std::string,std::string> zonedata)
{
	std::string idx;
	idx = dclient.devices[zonedata["zoneId"]].idx;
	if (verbose)
		cout << " - change status of zone " << zonedata["name"] << ": temperature = " << zonedata["temperature"] << ", setpoint = " << zonedata["targetTemperature"] << ", mode = " << zonedata["setpointMode"] << ", until = " << zonedata["until"] << endl;
	dclient.update_zone_status(idx, zonedata["temperature"], zonedata["targetTemperature"], zonedata["setpointMode"], zonedata["until"]);
}


int main(int argc, char** argv)
{
	// get current time
	now = time(0);

	// set defaults
	evoconfig["hwname"] = "evohome";
	configfile = CONF_FILE;


	if ( ! read_evoconfig() )
		exit_error(ERROR+"can't read config file");

	if (verbose)
		cout << "connect to Evohome server\n";
	EvohomeClient eclient = EvohomeClient(evoconfig["usr"],evoconfig["pw"]);

	if (strcmp(argv[2],"0") == 0 ) {
		// cancel override
		if ( ! eclient.cancel_temperature_override(string(argv[1])) )
			exit_error(ERROR+"failed to cancel override for zone "+argv[1]);

		eclient.full_installation();

		// get Evohome heating system
		EvohomeClient::temperatureControlSystem* tcs = NULL;
		if ( evoconfig.find("systemId") != evoconfig.end() )
		{
			if (verbose)
				cout << "using systemId from " << CONF_FILE << endl;
	 		tcs = eclient.get_temperatureControlSystem_by_ID(evoconfig["systemId"]);
			if (tcs == NULL)
				exit_error(ERROR+"the Evohome systemId specified in "+CONF_FILE+" cannot be found");
		}
		else if (eclient.is_single_heating_system())
			tcs = &eclient.locations[0].gateways[0].temperatureControlSystems[0];
		else
			select_temperatureControlSystem(eclient);
		if (tcs == NULL)
			exit_error(ERROR+"multiple Evohome systems found - don't know which one to use for status");

		// get status for Evohome heating system
		if (verbose)
			cout << "retrieve status of Evohome heating system\n";
		if ( !	eclient.get_status(tcs->locationId) )
			exit_error(ERROR+"failed to retrieve status");

		if ( reloadcache || ( ! eclient.read_schedules_from_file(SCHEDULE_CACHE) ) )
		{
			if (verbose)
				cout << "reloading schedules cache\n";
			if ( ! eclient.schedules_backup(SCHEDULE_CACHE) )
				exit_error(ERROR+"failed to open schedule cache file '"+SCHEDULE_CACHE+"'");
			eclient.read_schedules_from_file(SCHEDULE_CACHE);
		}
		if (verbose)
			cout << "read schedules from cache\n";

		DomoticzClient dclient = DomoticzClient(get_domoticz_host(evoconfig["url"], evoconfig["port"]));

		int hwid = dclient.get_hwid(HARDWARE_TYPE, evoconfig["hwname"]);
		if (verbose)
			cout << "got ID '" << hwid << "' for Evohome hardware with name '" << evoconfig["hwname"] << "'\n";

		if (hwid == -1)
			exit_error(ERROR+"evohome hardware not found");

		dclient.get_devices(hwid);

		// update zone
		for (std::map<int, EvohomeClient::zone>::iterator it=tcs->zones.begin(); it!=tcs->zones.end(); ++it)
		{
			if (string(argv[1]) == it->second.zoneId) {
				std::map<std::string, std::string> zonedata = evo_get_zone_data(tcs, it->first);
				zonedata["until"] = eclient.get_next_switchpoint_ex(tcs->zones[it->first].schedule, zonedata["temperature"]);
				update_zone(dclient, zonedata);
			}
		}
		dclient.cleanup();
		eclient.cleanup();
		exit(0);
	}

	std::string s_until = "";
	if (argc == 5)
	{
		// until set
		std::string utc_time = string(argv[4]);
		if (utc_time.length() < 19)
			exit_error(ERROR+"bad timestamp value on command line");
		struct tm ltime;
		ltime.tm_isdst = -1;
		ltime.tm_year = atoi(utc_time.substr(0, 4).c_str()) - 1900;
		ltime.tm_mon = atoi(utc_time.substr(5, 2).c_str()) - 1;
		ltime.tm_mday = atoi(utc_time.substr(8, 2).c_str());
		ltime.tm_hour = atoi(utc_time.substr(11, 2).c_str());
		ltime.tm_min = atoi(utc_time.substr(14, 2).c_str());
		ltime.tm_sec = atoi(utc_time.substr(17, 2).c_str());
		time_t ntime = mktime(&ltime);
		if ( ntime == -1)
			exit_error(ERROR+"bad timestamp value on command line");
		char c_until[40];
		sprintf(c_until,"%04d-%02d-%02dT%02d:%02d:%02dZ",ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday,ltime.tm_hour,ltime.tm_min,ltime.tm_sec);
		s_until = string(c_until);
	}

	eclient.set_temperature(string(argv[1]), string(argv[3]), s_until);


	if (s_until != "")
	{
		// correct UTC until time in Domoticz

		if (verbose)
			cout << "connect to Domoticz server\n";
		DomoticzClient dclient = DomoticzClient(get_domoticz_host(evoconfig["url"], evoconfig["port"]));

		int hwid = dclient.get_hwid(HARDWARE_TYPE, evoconfig["hwname"]);
		if (verbose)
			cout << "got ID '" << hwid << "' for Evohome hardware with name '" << evoconfig["hwname"] << "'\n";

		if (hwid == -1)
			exit_error(ERROR+"evohome hardware not found");

		dclient.get_devices(hwid);

		std::string idx = dclient.devices[argv[1]].idx;
		std::string temperature = dclient.devices[argv[1]].Temp;

		dclient.update_zone_status(idx, temperature, string(argv[3]), "TemporaryOverride", utc_to_local(s_until).substr(0,19));

		dclient.cleanup();
	}

	eclient.cleanup();
	return 0;
}
