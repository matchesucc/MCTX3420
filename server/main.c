/**
 * @file main.c
 * @brief main and its helper functions, signal handling and cleanup functions
 */

// --- Custom headers --- //
#include "common.h"
#include "options.h"
#include "sensor.h"
#include "actuator.h"
#include "control.h"

// --- Standard headers --- //
#include <syslog.h> // for system logging
#include <signal.h> // for signal handling

// --- Variable definitions --- //
Options g_options; // options passed to program through command line arguments

// --- Function definitions --- //

/**
 * Parse command line arguments, initialise g_options
 * @param argc - Number of arguments
 * @param argv - Array of argument strings
 */
void ParseArguments(int argc, char ** argv)
{
	g_options.program = argv[0]; // program name
	g_options.verbosity = LOGDEBUG; // default log level
	gettimeofday(&(g_options.start_time), NULL); // Start time
	Log(LOGDEBUG, "Called as %s with %d arguments.", g_options.program, argc);
}

/**
 * Handle a signal
 * @param signal - The signal number
 */
//TODO: Something that gets massively annoying with threads is that you can't predict which one gets the signal
// There are ways to deal with this, but I can't remember them
// Probably sufficient to just call Thread_QuitProgram here
void SignalHandler(int signal)
{
	// At the moment just always exit.
	// Call `exit` so that Cleanup will be called to... clean up.
	Log(LOGWARN, "Got signal %d (%s). Exiting.", signal, strsignal(signal));

	//exit(signal);
}

/**
 * Cleanup before the program exits
 */
void Cleanup()
{
	Log(LOGDEBUG, "Begin cleanup.");
	Log(LOGDEBUG, "Finish cleanup.");

}

/**
 * Main entry point; start worker threads, setup signal handling, wait for threads to exit, exit
 * @param argc - Num args
 * @param argv - Args
 * @returns 0 on success, error code on failure
 * NOTE: NEVER USE exit(3)! Instead call Thread_QuitProgram
 */
int main(int argc, char ** argv)
{
	ParseArguments(argc, argv);

	//Open the system log
	openlog("mctxserv", LOG_PID | LOG_PERROR, LOG_USER);
	Log(LOGINFO, "Server started");
	// signal handler
	//TODO: Make this work
	/*
	int signals[] = {SIGINT, SIGSEGV, SIGTERM};
	for (int i = 0; i < sizeof(signals)/sizeof(int); ++i)
	{
		signal(signals[i], SignalHandler);
	}
	*/
	Sensor_Init();
	Actuator_Init();
	//Sensor_StartAll("test");
	//Actuator_StartAll("test");
	const char *ret;
	if ((ret = Control_SetMode(CONTROL_START, "test")) != NULL)
		Fatal("Control_SetMode failed with '%s'", ret);

	// run request thread in the main thread
	FCGI_RequestLoop(NULL);

	if ((ret = Control_SetMode(CONTROL_STOP, "test")) != NULL)
		Fatal("Control_SetMode failed with '%s'", ret);
	//Sensor_StopAll();
	//Actuator_StopAll();

	Cleanup();
	return 0;
}


