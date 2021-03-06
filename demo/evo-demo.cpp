/*
 * Copyright (c) 2017 Gordon Bos <gordon@bosvangennip.nl> All rights reserved.
 *
 * Demo app for connecting to Evohome
 *
 */

#include <cstdlib>
#include <string>
#include <iostream>
#include <map>
#include <time.h>


#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <cstring>
#include <time.h>
#include "../evohomeclient/evohomeclient.h"
#include "../evohomeclient/evohomeoldclient.h"

#ifndef CONF_FILE
#define CONF_FILE "evoconfig"
#endif

#ifndef SCHEDULE_CACHE
#define SCHEDULE_CACHE "schedules.json"
#endif

using namespace std;


time_t now;
int tzoffset=-1;
std::string lastzone = "";

std::string configfile;
std::map<std::string, std::string> evoconfig;

bool verbose;

std::string ERROR = "ERROR: ";
std::string WARN = "WARNING: ";


bool read_evoconfig()
{
	ifstream myfile (configfile.c_str());
	if ( myfile.is_open() )
	{
		std::stringstream key,val;
		bool isKey = true;
		std::string line;
		unsigned int i;
		while ( getline(myfile,line) )
		{
			if ( (line[0] == '#') || (line[0] == ';') )
				continue;
			for (i = 0; i < line.length(); i++)
			{
				if ( (line[i] == ' ') || (line[i] == '\'') || (line[i] == '"') || (line[i] == 0x0d) )
					continue;
				if (line[i] == '=')
				{
					isKey = false;
					continue;
				}
				if (isKey)
					key << line[i];
				else
					val << line[i];
			}
			if ( ! isKey )
			{
				std::string skey = key.str();
				evoconfig[skey] = val.str();
				isKey = true;
				key.str("");
				val.str("");
			}
		}
		myfile.close();
		return true;
	}
	return false;
}


void exit_error(std::string message)
{
	cerr << message << endl;
	exit(1);
}


EvohomeClient::temperatureControlSystem* select_temperatureControlSystem(EvohomeClient &eclient)
{
	int location = 0;
	int gateway = 0;
	int temperatureControlSystem = 0;
	bool is_unique_heating_system = false;
	if ( evoconfig.find("location") != evoconfig.end() ) {
		if (verbose)
			cout << "using location from " << configfile << endl;
		int l = eclient.locations.size();
		location = atoi(evoconfig["location"].c_str());
		if (location > l)
			exit_error(ERROR+"the Evohome location specified in "+configfile+" cannot be found");
		is_unique_heating_system = ( (eclient.locations[location].gateways.size() == 1) &&
						(eclient.locations[location].gateways[0].temperatureControlSystems.size() == 1)
						);
	}
	if ( evoconfig.find("gateway") != evoconfig.end() ) {
		if (verbose)
			cout << "using gateway from " << configfile << endl;
		int l = eclient.locations[location].gateways.size();
		gateway = atoi(evoconfig["gateway"].c_str());
		if (gateway > l)
			exit_error(ERROR+"the Evohome gateway specified in "+configfile+" cannot be found");
		is_unique_heating_system = (eclient.locations[location].gateways[gateway].temperatureControlSystems.size() == 1);
	}
	if ( evoconfig.find("controlsystem") != evoconfig.end() ) {
		if (verbose)
			cout << "using controlsystem from " << configfile << endl;
		int l = eclient.locations[location].gateways[gateway].temperatureControlSystems.size();
		temperatureControlSystem = atoi(evoconfig["controlsystem"].c_str());
		if (temperatureControlSystem > l)
			exit_error(ERROR+"the Evohome temperature controlsystem specified in "+configfile+" cannot be found");
		is_unique_heating_system = true;
	}


	if ( ! is_unique_heating_system)
		return NULL;

	return &eclient.locations[location].gateways[gateway].temperatureControlSystems[temperatureControlSystem];
}


/*
 * Create an associative array with the zone information we need
 */
std::map<std::string,std::string> evo_get_zone_data(EvohomeClient::zone *zone)
{
	map<std::string,std::string> ret;
	ret["zoneId"] = (*zone->status)["zoneId"].asString();
	ret["name"] = (*zone->status)["name"].asString();
	ret["temperature"] = (*zone->status)["temperatureStatus"]["temperature"].asString();
	ret["setpointMode"] = (*zone->status)["heatSetpointStatus"]["setpointMode"].asString();
	ret["targetTemperature"] = (*zone->status)["heatSetpointStatus"]["targetTemperature"].asString();
	ret["until"] = (*zone->status)["heatSetpointStatus"]["until"].asString();
	return ret;
}


int main(int argc, char** argv)
{
// get current time
	now = time(NULL);

// get settings from config file
	configfile = CONF_FILE;
	read_evoconfig();

// connect to Evohome server
	std::cout << "connect to Evohome server\n";
	EvohomeClient eclient = EvohomeClient();
	if (eclient.load_auth_from_file("/tmp/evo2auth.json"))
		std::cout << "    reusing saved connection (UK/EMEA)\n";
	else if (eclient.login(evoconfig["usr"],evoconfig["pw"]))
		std::cout << "    connected (UK/EMEA)\n";

	EvohomeOldClient v1client = EvohomeOldClient();
	if (v1client.load_auth_from_file("/tmp/evo1auth.json"))
		std::cout << "    reusing saved connection (US)\n";
	else if (v1client.login(evoconfig["usr"],evoconfig["pw"]))
		std::cout << "    connected (US)\n";

// retrieve Evohome installation
	std::cout << "retrieve Evohome installation\n";
	eclient.full_installation();
	v1client.full_installation();

// set Evohome heating system
	int location = 0;
	int gateway = 0;
	int temperatureControlSystem = 0;

	if ( evoconfig.find("locationId") != evoconfig.end() )
	{
		while ( (eclient.locations[location].locationId != evoconfig["locationId"])  && (location < (int)eclient.locations.size()) )
			location++;
		if (location == (int)eclient.locations.size())
			exit_error(ERROR+"the Evohome location ID specified in "+CONF_FILE+" cannot be found");
	}
	if ( evoconfig.find("gatewayId") != evoconfig.end() )
	{
		while ( (eclient.locations[location].gateways[gateway].gatewayId != evoconfig["gatewayId"])  && (gateway < (int)eclient.locations[location].gateways.size()) )
			gateway++;
		if (gateway == (int)eclient.locations[location].gateways.size())
			exit_error(ERROR+"the Evohome gateway ID specified in "+CONF_FILE+" cannot be found");
	}
	if ( evoconfig.find("systemId") != evoconfig.end() )
	{
		while ( (eclient.locations[location].gateways[gateway].temperatureControlSystems[temperatureControlSystem].systemId != evoconfig["systemId"])  && (temperatureControlSystem < (int)eclient.locations[location].gateways[gateway].temperatureControlSystems.size()) )
			temperatureControlSystem++;
		if (temperatureControlSystem == (int)eclient.locations[location].gateways[gateway].temperatureControlSystems.size())
			exit_error(ERROR+"the Evohome system ID specified in "+CONF_FILE+" cannot be found");
	}
	EvohomeClient::temperatureControlSystem* tcs = &eclient.locations[location].gateways[gateway].temperatureControlSystems[temperatureControlSystem];


// retrieve Evohome status
	std::cout << "retrieve Evohome status\n";
	if ( !	eclient.get_status(location) )
		std::cout << "status fail" << "\n";
/*
	std::cout << "\nDump of full installationinfo\n";
	std::cout << eclient.j_fi.toStyledString() << "\n";
*/
/*
	std::cout << "\nDump of full status\n";
	std::cout << eclient.j_stat.toStyledString() << "\n";
*/
/*
	std::cout << "\nDump of full installationinfo\n";
	std::cout << v1client.j_fi.toStyledString() << "\n";
*/

// retrieving schedules and/or switchpoints can be slow because we can only fetch them for a single zone at a time.
// luckily schedules do not change very often, so we can use a local cache
	if ( ! eclient.read_schedules_from_file(SCHEDULE_CACHE) )
	{
		std::cout << "create local copy of schedules" << "\n";
		if ( ! eclient.schedules_backup(SCHEDULE_CACHE) )
			exit_error(ERROR+"failed to open schedule cache file '"+SCHEDULE_CACHE+"'");
		eclient.read_schedules_from_file(SCHEDULE_CACHE);
	}


// start demo output
	std::cout << "\nSystem info:\n";
	std::cout << "    Model Type = " << (*tcs->installationInfo)["modelType"] << "\n";
	std::cout << "    System ID = " << (*tcs->installationInfo)["systemId"] << "\n";
	std::cout << "    System mode = " << (*tcs->status)["systemModeStatus"]["mode"] << "\n";

	std::cout << "\nZones:\n";
	std::cout << "      ID       temp    v1temp      mode          setpoint      until               name\n";
	for (std::vector<EvohomeClient::zone>::size_type i = 0; i < tcs->zones.size(); ++i)
	{
		std::map<std::string,std::string> zone = evo_get_zone_data(&tcs->zones[i]);
		if (zone["until"].length() == 0)
		{
//			zone["until"] = eclient.request_next_switchpoint(zone["zoneId"]); // ask web portal (UTC)
			zone["until"] = eclient.get_next_switchpoint(zone["zoneId"]); // find in schedule (localtime)
//			zone["until"] = eclient.get_next_utcswitchpoint(zone["zoneId"]); // find in schedule (UTC)

			// get_next_switchpoint returns an assumed time zone indicator 'A' which only means to
			// differentiate from the UTC time zone indicator 'Z'. It's beyond the scope of this demo
			// and library to find the actual value for your timezone.
			if ((zone["until"].size() > 19) && (zone["until"][19] == 'A'))
				zone["until"] = zone["until"].substr(0,19);
		}
		else if (zone["until"].length() >= 19)
		{
			// Honeywell is mixing UTC and localtime in their returns
			// for display we need to convert overrides to localtime
			if (tzoffset == -1)
			{
				// calculate timezone offset once
				struct tm utime;
				gmtime_r(&now, &utime);
				tzoffset = difftime(mktime(&utime), now);
			}
			struct tm ltime;
			localtime_r(&now, &ltime);
			ltime.tm_isdst = -1;
			ltime.tm_hour = atoi(zone["until"].substr(11, 2).c_str());
			ltime.tm_min = atoi(zone["until"].substr(14, 2).c_str());
			ltime.tm_sec = atoi(zone["until"].substr(17, 2).c_str()) - tzoffset;
			mktime(&ltime);
			char until[40];
			sprintf(until,"%04d-%02d-%02dT%02d:%02d:%02d",ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday,ltime.tm_hour,ltime.tm_min,ltime.tm_sec);
			zone["until"] = string(until);
		}

		std::cout << "    " << zone["zoneId"];
		std::cout << " => " << zone["temperature"];
		std::cout << " => " << v1client.get_zone_temperature(tcs->zones[i].locationId, zone["zoneId"], 1);
		std::cout << " => " << zone["setpointMode"];
		std::cout << " => " << zone["targetTemperature"];
		std::cout << " => " << zone["until"];
		std::cout << " => " << zone["name"];
		std::cout << "\n";

		lastzone = zone["zoneId"];
	}

	std::cout << "\n";

// Dump json to screen
/*
	EvohomeClient::zone* myzone = eclient.get_zone_by_ID(lastzone);
	std::cout << "\nDump of installationinfo for zone" << lastzone << "\n";
	std::cout << (*myzone->installationInfo).toStyledString() << "\n";
	std::cout << "\nDump of statusinfo for zone" << lastzone << "\n";
	std::cout << (*myzone->status).toStyledString() << "\n";
*/

/*
	std::cout << "\nDump of full installationinfo\n";
	std::cout << eclient.j_fi.toStyledString() << "\n";
*/
/*
	std::cout << "\nDump of full status\n";
	std::cout << eclient.j_stat.toStyledString() << "\n";
*/
/*
	std::cout << "\nDump of full installationinfo\n";
	std::cout << v1client.j_fi.toStyledString() << "\n";
*/

eclient.save_auth_to_file("/tmp/evo2auth.json");
v1client.save_auth_to_file("/tmp/evo1auth.json");

	eclient.cleanup();
	v1client.cleanup();

	return 0;
}

