#pragma once

//#define USE_PANGOLIN
//#define HOMIELIB_VERBOSE
#if !defined(USE_PANGOLIN) && !defined(USE_ARDUINOMQTT) && !defined(USE_ASYNCMQTTCLIENT) && !defined(USE_PUBSUBCLIENT)
	#define USE_PUBSUBCLIENT
#endif

#ifdef USE_ARDUINOMQTT
	#ifndef ARDUINOMQTT_BUFSIZE
	#define ARDUINOMQTT_BUFSIZE 256
	#endif
#endif
