#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <iostream>
#include <fstream>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

using std::string;
using std::vector;
using std::fstream;

enum Status {
	SUCCESS,
	SUCCESS_SOCKET_STARTED,
	SUCCESS_SOCKET_DATA_AVAILABLE,
	SUCCESS_SOCKET_DATA_READY,
	SUCCESS_SOCKET_DATA_NOT_READY,
	FAILURE_SOCKET_UNEXPECTED,
	FAILURE_ASIC_SOURCE_FILE,
	FAILURE_ASIC_OUTPUT_FILE,
	FAILURE_ASIC_OUTPUT_DATA,
	FAILURE_CORRECTION,
	FAILURE_SOCKET_START_SOCKET,
	FAILURE_SOCKET_START_REUSEADDR,
	FAILURE_SOCKET_START_REUSEPORT,
	FAILURE_SOCKET_START_BIND,
	FAILURE_SOCKET_START_CONNECT,
	FAILURE_SOCKET_START_WRITE,
	FAILURE_SOCKET_WRITE,
	FAILURE_SOCKET_READ
};

enum ArgumentIndex {
	USE_SAMPLING = 1,
	USE_HARMONIC_MEAN,
	SLIDING_WINDOW_SIZE,
	BUCKETS_ARRAY_SIZE,
	Y2U_ARRAY_SIZE,
	X2Y_SAMPLING_SIZE,
	ARGUMENTS_COUNT_SHIFTED
};

#define SCRIPT_IP	"127.0.0.1"
#define SCRIPT_PORT	50007
#define DRIVER_PORT 0
#define SOCK_BUF_SZ 63

class Driver {
private:
	unsigned long long** _uWindow;
	unsigned long long*	 _meanWindow;
	unsigned long long*	 _uDataRegister;
	unsigned long long	 _meanDataRegister;
	bool 				 _isUwindowFull;	
	unsigned int		 _currentTimeSlot;
	unsigned int		 _numberOfSingleElementsInUwindow;
	unsigned long long   _cardinalityEstimation;
	int					 _socket;
	char				 _socketBuffer[SOCK_BUF_SZ + 1];
	double				 _biasCorrectionElement;
	bool				 _useSampling;
	bool				 _useHarmonicMean;
	unsigned int		 _slidingWindowSize;
	unsigned int		 _bucketsArraySize;
	unsigned int		 _y2uArraySize;
	unsigned int		 _x2ySamplingSize;
	static const string  _useSamplingMacro;
	static const string  _useHarmonicMeanMacro;
	static const string  _bucketsArraySizeMacro;
	static const string  _bucketsArrayTruncMaskMacro;
	static const string  _x2ySamplingTruncMaskMacro;
	static const string  _y2uArraySizeMacro;
	static const string  _uDataRegisterName;
	static const string  _meanDataRegisterName;
	static const string  _asicSourceFilePath;
	static const string  _asicOutputFilePath;
public:
	Driver();
	~Driver();
	Status open(int argumentsCount, char* argumentsVector[]);
	void   processData();
	void   close();
private:
	Status updateArguments(int argumentsCount, char* argumentsVector[]);
	void   initializeDataStructures();
	void   getBiasCorrectionElement();
	Status establishCommunication();
	Status checkForData();
	Status readAsicRegisters();
	void   estimateMean();
	void   estimateSingleElementsNumber();
	Status outputCardinalityEstimation();
	Status sendDriverStatus(Status status);	
	Status sendDriverOutput();	
};
