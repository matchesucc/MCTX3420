/**
 * @file fastcgi.c
 * @purpose Runs the FCGI request loop to handle web interface requests.
 *
 * fcgi_stdio.h must be included before all else so the stdio function
 * redirection works ok.
 */

#include <fcgi_stdio.h>
#include <openssl/sha.h>
#include "fastcgi.h"
#include "common.h"
#include "sensor.h"
#include "log.h"
#include <time.h>

static void LoginHandler(void *data, char *params) {
	static char loginkey[41] = {0}, ip[256];
	static time_t timestamp = 0;
	const char *key, *value;
	int force = 0, end = 0;

	while ((params = FCGI_KeyPair(params, &key, &value))) {
		if (!strcmp(key, "force"))
			force = !force;
		else if (!strcmp(key, "end"))
			end = !end;
	}

	if (end) {
		*loginkey = 0;
		FCGI_BeginJSON(200, "login");
		FCGI_EndJSON();
		return;
	}

	time_t now = time(NULL);
	if (force || !*loginkey || (now - timestamp > 180)) {
		SHA_CTX sha1ctx;
		unsigned char sha1[20];
		int i = rand();

		SHA1_Init(&sha1ctx);
		SHA1_Update(&sha1ctx, &now, sizeof(now));
		SHA1_Update(&sha1ctx, &i, sizeof(i));
		SHA1_Final(sha1, &sha1ctx);

		timestamp = now;
		for (i = 0; i < 20; i++)
			sprintf(loginkey+i*2, "%02x", sha1[i]);
		sprintf(ip, "%s", getenv("REMOTE_ADDR"));
		FCGI_BeginJSON(200, "login");
		FCGI_BuildJSON("key", loginkey);
		FCGI_EndJSON();
	} else {
		char buf[128];
		strftime(buf, 128, "%H:%M:%S %d-%m-%Y",localtime(&timestamp)); 
		FCGI_BeginJSON(401, "login");
		FCGI_BuildJSON("description", "Already logged in");
		FCGI_BuildJSON("user", ip); 
		FCGI_BuildJSON("time", buf);
		FCGI_EndJSON();
	}
}

/**
 * Handle a request to the sensor module
 * @param data - Data to pass to module (?)
 * @param params - Parameters passed
 */
static void SensorHandler(void * data, char * params)
{
	static DataPoint buffer[SENSOR_QUERYBUFSIZ];
	StatusCodes status = STATUS_OK;
	const char * key; const char * value;

	int sensor_id = SENSOR_NONE;

	while ((params = FCGI_KeyPair(params, &key, &value)) != NULL)
	{
		Log(LOGDEBUG, "Got key=%s and value=%s", key, value);
		if (strcmp(key, "id") == 0)
		{
			if (sensor_id != SENSOR_NONE)
			{
				Log(LOGERR, "Only one sensor id should be specified");
				status = STATUS_BADREQUEST;
				break;
			}
			//TODO: Use human readable sensor identifier string for API?
			sensor_id = atoi(value);
			if (sensor_id == 0 && strcmp(value, "0") != 0)
			{
				Log(LOGERR, "Sensor id not an integer; %s", value);
				status = STATUS_BADREQUEST;
				break;
			}
		}
		else
		{
			Log(LOGERR, "Unknown key \"%s\" (value = %s)", key, value);
			status = STATUS_BADREQUEST;
			break;
		}		
	}

	if (sensor_id == SENSOR_NONE)
	{
		Log(LOGERR, "No sensor id specified");
		status = STATUS_BADREQUEST;
	}
	else if (sensor_id >= NUMSENSORS || sensor_id < 0)
	{
		Log(LOGERR, "Invalid sensor id %d", sensor_id);
		status = STATUS_BADREQUEST;
	}

	FCGI_BeginJSON(status, "sensor");
	
	if (status != STATUS_BADREQUEST)
	{
		FCGI_BuildJSON(key, value); // should spit back sensor ID
		//Log(LOGDEBUG, "Call Sensor_Query...");
		int amount_read = Sensor_Query(&(g_sensors[sensor_id]), buffer, SENSOR_QUERYBUFSIZ);
		//Log(LOGDEBUG, "Read %d DataPoints", amount_read);
		//Log(LOGDEBUG, "Produce JSON response");
		printf(",\r\n\t\"data\" : [");
		for (int i = 0; i < amount_read; ++i)
		{
			printf("[%f,%f]", buffer[i].time, buffer[i].value);
			if (i+1 < amount_read)
				printf(",");
		}
		printf("]");
		//Log(LOGDEBUG, "Done producing JSON response");
	}
	FCGI_EndJSON();		
	
}

/**
 * Extracts a key/value pair from a request string.
 * Note that the input is modified by this function.
 * @param in The string from which to extract the pair
 * @param key A pointer to a variable to hold the key string
 * @param value A pointer to a variable to hold the value string
 * @return A pointer to the start of the next search location, or NULL if
 *         the EOL is reached.
 */
char *FCGI_KeyPair(char *in, const char **key, const char **value)
{
	char *ptr;
	if (!in || !*in) { //Invalid input or string is EOL
		return NULL;
	}

	*key = in;
	//Find either = or &, whichever comes first
	if ((ptr = strpbrk(in, "=&"))) {
		if (*ptr == '&') { //No value specified
			*value = ptr;
			*ptr++ = 0;
		} else {
			//Stopped at an '=' sign
			*ptr++ = 0;
			*value = ptr;
			if ((ptr = strchr(ptr,'&'))) {
				*ptr++ = 0;
			} else {
				ptr = "";
			}
		}
	} else { //No value specified and no other pair
		ptr = "";
		*value = ptr;
	}
	return ptr;
}

/**
 * Begins a response to the client in JSON format.
 * @param status_code The HTTP status code to be returned.
 * @param module The name of the module that initiated the response.
 */
void FCGI_BeginJSON(StatusCodes status_code, const char *module)
{
	switch (status_code) {
		case STATUS_OK:
			break;
		case STATUS_UNAUTHORIZED:
			printf("Status: 401 Unauthorized\r\n");
			break;
		default:
			printf("Status: 400 Bad Request\r\n");
	}
	printf("Content-type: application/json; charset=utf-8\r\n\r\n");
	printf("{\r\n");
	printf("\t\"module\" : \"%s\"", module);
}

/**
 * Adds a key/value pair to a JSON response. The response must have already
 * been initiated by FCGI_BeginJSON. Note that characters are not escaped.
 * @param key The key of the JSON entry
 * &param value The value associated with the key.
 */
void FCGI_BuildJSON(const char *key, const char *value)
{
	printf(",\r\n\t\"%s\" : \"%s\"", key, value);
}

/**
 * Ends a JSON response that was initiated by FCGI_BeginJSON.
 */
void FCGI_EndJSON() 
{
	printf("\r\n}\r\n");
}

/**
 * Main FCGI request loop that receives/responds to client requests.
 * @param data A data field to be passed to the selected module handler.
 */ 
void FCGI_RequestLoop (void * data)
{
	int count = 0;
	while (FCGI_Accept() >= 0)   {
		ModuleHandler module_handler = NULL;
		char module[BUFSIZ], params[BUFSIZ];

		//strncpy doesn't zero-truncate properly
		snprintf(module, BUFSIZ, "%s", getenv("DOCUMENT_URI_LOCAL"));
		snprintf(params, BUFSIZ, "%s", getenv("QUERY_STRING"));
		
		//Remove trailing slashes (if present) from module query
		size_t lastchar = strlen(module) - 1;
		if (lastchar > 0 && module[lastchar] == '/')
			module[lastchar] = 0;
		

		if (!strcmp("sensors", module)) {
			module_handler = SensorHandler;
		} else if (!strcmp("login", module)) {
			module_handler = LoginHandler;
		} else if (!strcmp("actuators", module)) {
			
		}

		if (module_handler) {
			module_handler(data, params);
		} else {
			char buf[BUFSIZ];
			
			FCGI_BeginJSON(400, module);
			FCGI_BuildJSON("description", "400 Invalid response");
			snprintf(buf, BUFSIZ, "%d", count);
			FCGI_BuildJSON("request-number", buf);
			FCGI_BuildJSON("params", params);
			FCGI_BuildJSON("host", getenv("SERVER_HOSTNAME"));
			FCGI_BuildJSON("user", getenv("REMOTE_USER"));
			FCGI_BuildJSON("userip", getenv("REMOTE_ADDR"));
			FCGI_EndJSON();
		}

		count++;
	}
}
