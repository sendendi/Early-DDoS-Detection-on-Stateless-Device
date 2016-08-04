#include "driver.h"


/*************************************************************************************************
* Function:			main()
* 
* Description:		This is a main Driver function; it starts the Driver and processes the incoming data 			
* Input:			argc - arguments count; the count is constant (no mater whether traffic sampling is used or not)
*					argv - arguments vector
* Output:			SUCCESS/FAILURE indication
*************************************************************************************************/
int main(int argc, char* argv[]) {

	Driver driver;
	Status status = driver.open(argc, argv);
	if (status == SUCCESS) {
		driver.processData();
	}
	return status;
}

/***D R I V E R  C L A S S:**********************************************************************/

const string Driver::_useSamplingMacro = "USE_SAMPLING";
const string Driver::_useHarmonicMeanMacro = "CARD_USE_HARMONIC_MEAN";
const string Driver::_bucketsArraySizeMacro	= "CARD_BUCKETS_ARR_SIZE";
const string Driver::_bucketsArrayTruncMaskMacro = "CARD_BUCKETS_TRUNC_MASK";
const string Driver::_x2ySamplingTruncMaskMacro	= "SAMPLE_X2Y_TRUNC_MASK";
const string Driver::_y2uArraySizeMacro	= "SAMPLE_Y2U_ARR_SIZE";
const string Driver::_uDataRegisterName = "sample_y2u_data_register";
const string Driver::_meanDataRegisterName = "cardinality_mean_register";
const string Driver::_asicSourceFilePath = "/home/victor/Downloads/bmv2/targets/ddos_switch/ddos_switch.p4";
const string Driver::_asicOutputFilePath = "/home/victor/Downloads/bmv2/targets/ddos_switch/ddos_switch_output.txt";
	
/*************************************************************************************************
* Function:			Driver()
* 
* Description:		Constructor			
* Input:			None	
* Output:			None
*************************************************************************************************/
Driver::Driver() : _isUwindowFull(false), _cardinalityEstimation(0), _numberOfSingleElementsInUwindow(0), _currentTimeSlot(0) {}

/*************************************************************************************************
* Function:			~Driver()
* 
* Description:		Destructor			
* Input:			None
* Output:			None
*************************************************************************************************/
Driver::~Driver() {

	// close the communication:
	close();
	// release dynamic memory:
	for (unsigned int i = 0; i < _slidingWindowSize; i++) {
		delete[] _uWindow[i];
	}
	delete[] _meanWindow;
	delete[] _uDataRegister;
}

/*************************************************************************************************
* Function:			open()
* 
* Description:		This function opens the Driver:
*					- it re-writes p4 ASIC code according to input parameters
*					- calculates bias correction element
*					- and establishes socket communication with the Script 		
* Input:			argumentsCount - arguments count; the count is constant (no mater whether traffic sampling is used or not)
*					argumentsVector - arguments vector
* Output:			SUCCESS/FAILURE indication
*************************************************************************************************/
Status Driver::open(int argumentsCount, char* argumentsVector[]) {

	Status status = updateArguments(argumentsCount, argumentsVector);
	if (status == SUCCESS) {
		initializeDataStructures();
		getBiasCorrectionElement();
		status = establishCommunication();
	}
	return status;
}

/*************************************************************************************************
* Function:			processData()
* 
* Description:		This function processes incoming data:
*					- it checks for the data
*					- if new data is available it reads ASIC registers
*					- estimates the mean
*					- estimates the number of the single elements when traffic sampling is used
*					- and outputs cardinality estimation		
* Input:			None
* Output:			None
*************************************************************************************************/
void Driver::processData() {

	for (;;) {
		Status status = checkForData();
		if (status == SUCCESS) {
			status = readAsicRegisters();
			if (status == SUCCESS) {
				estimateMean();
				estimateSingleElementsNumber();
				outputCardinalityEstimation();				
			}
		}
	}
}

/*************************************************************************************************
* Function:			close()
* 
* Description:		This function closes the Driver			
* Input:			None
* Output:			None
*************************************************************************************************/
void Driver::close() {
	
	// disconnect the socket:
	if (_socket >= 0) {
		shutdown(_socket, SHUT_RDWR);
		::close(_socket);
	}
}

/*************************************************************************************************
* Function:			updateArguments()
* 
* Description:		This function re-writes p4 ASIC code MACRO lines according to input Driver parameters		
* Input:			argumentsCount - arguments count; the count is constant (no mater whether traffic sampling is used or not)
*					argumentsVector - arguments vector
* Output:			SUCCESS/FAILURE indication
*************************************************************************************************/
static string updateMacroLine(string asicSourceFileLine, string macroString, int argumentIndex, char* argumentsVector[]) {

	string::size_type index = asicSourceFileLine.find(macroString);
	if (index != string::npos) {
		index += macroString.length();
		return asicSourceFileLine = asicSourceFileLine.substr(0, index) + ' ' + argumentsVector[argumentIndex];
	}
	return "";
}

static string updateMacroLine(string asicSourceFileLine, string macroString, unsigned long long newValue) {

	string::size_type index = asicSourceFileLine.find(macroString);
	if (index != string::npos) {
		index += macroString.length();
		return asicSourceFileLine = asicSourceFileLine.substr(0, index) + ' ' + std::to_string(newValue);
	}
	return "";
}

Status Driver::updateArguments(int argumentsCount, char* argumentsVector[]) {

	// parse command line arguments:
	assert(argumentsCount == ARGUMENTS_COUNT_SHIFTED);
	_useSampling = (bool)(std::stoi(argumentsVector[USE_SAMPLING]));
	_useHarmonicMean = (bool)(std::stoi(argumentsVector[USE_HARMONIC_MEAN]));
	_slidingWindowSize = (unsigned int)(std::stoi(argumentsVector[SLIDING_WINDOW_SIZE]));
	_bucketsArraySize = (unsigned int)(std::stoi(argumentsVector[BUCKETS_ARRAY_SIZE]));
	_y2uArraySize = (unsigned int)(std::stoi(argumentsVector[Y2U_ARRAY_SIZE]));
	_x2ySamplingSize = (unsigned int)(std::stoi(argumentsVector[X2Y_SAMPLING_SIZE]));
	// get additional parameters:
	unsigned int bucketsArrayTruncMask = _bucketsArraySize - 1;
	unsigned int x2ySamplingTruncMask = _x2ySamplingSize - 1;
	// open ASIC source file:
	fstream asicSourceFileStream;
	asicSourceFileStream.open(_asicSourceFilePath, fstream::in);
	if (!asicSourceFileStream.is_open()) {
		return FAILURE_ASIC_SOURCE_FILE;
	}
	// read and update ASIC source file:
	const string macroDirective = "#define";
	const string newLine = "\n";
	string asicSourceFileLine;
	vector<string> asicSourceFileLines;
	while (getline(asicSourceFileStream, asicSourceFileLine) != nullptr) {
		// update only MACRO lines:
		if (asicSourceFileLine.find(macroDirective) != string::npos) {
			string asicSourceFileLineUpdated = updateMacroLine(asicSourceFileLine, _useSamplingMacro, USE_SAMPLING, argumentsVector);
			if (asicSourceFileLineUpdated.empty()) {
				asicSourceFileLineUpdated = updateMacroLine(asicSourceFileLine, _useHarmonicMeanMacro, USE_HARMONIC_MEAN, argumentsVector);
				if (asicSourceFileLineUpdated.empty()) {
					asicSourceFileLineUpdated = updateMacroLine(asicSourceFileLine, _bucketsArraySizeMacro, BUCKETS_ARRAY_SIZE, argumentsVector);
					if (asicSourceFileLineUpdated.empty()) {
						asicSourceFileLineUpdated = updateMacroLine(asicSourceFileLine, _bucketsArrayTruncMaskMacro, bucketsArrayTruncMask);
						if (asicSourceFileLineUpdated.empty()) {
							asicSourceFileLineUpdated = updateMacroLine(asicSourceFileLine, _x2ySamplingTruncMaskMacro, x2ySamplingTruncMask);
							if (asicSourceFileLineUpdated.empty()) {
								asicSourceFileLineUpdated = updateMacroLine(asicSourceFileLine, _y2uArraySizeMacro, Y2U_ARRAY_SIZE, argumentsVector);
								if (asicSourceFileLineUpdated.empty()) {
									asicSourceFileLineUpdated = asicSourceFileLine;
								}
							}
						}
					}
				}
			}
			asicSourceFileLine = asicSourceFileLineUpdated;
		}
		asicSourceFileLines.push_back(asicSourceFileLine);
	}
	// re-open ASIC source file:
	asicSourceFileStream.close();
	asicSourceFileStream.open(_asicSourceFilePath, fstream::out);
	assert(asicSourceFileStream.is_open());
	// re-write ASIC source file:
	for (unsigned int i = 0; i < asicSourceFileLines.size(); i++) {
		asicSourceFileLine = asicSourceFileLines[i] + newLine;
		asicSourceFileStream.write(asicSourceFileLine.c_str(), asicSourceFileLine.size());
	}
	// close ASIC source file:
	asicSourceFileStream.flush();
	asicSourceFileStream.close();
	return SUCCESS;
}

/*************************************************************************************************
* Function:			initializeDataStructures()
* 
* Description:		This function allocates the memory according to input Driver parameters 			
* Input:			None
* Output:			None
*************************************************************************************************/
void Driver::initializeDataStructures() {

	_uWindow = new unsigned long long*[_y2uArraySize];
	for (unsigned int i = 0; i < _y2uArraySize; i++) {
		_uWindow[i] = new unsigned long long[_slidingWindowSize];
		memset(_uWindow[i], 0, _slidingWindowSize * sizeof(unsigned long long));
	}
	_meanWindow = new unsigned long long[_slidingWindowSize];
	memset(_meanWindow, 0, _slidingWindowSize * sizeof(unsigned long long));
	_uDataRegister = new unsigned long long[_y2uArraySize];
	memset(_uDataRegister, 0, _y2uArraySize * sizeof(unsigned long long));
	memset(_socketBuffer, 0, sizeof(_socketBuffer));
}

/*************************************************************************************************
* Function:			getBiasCorrectionElement()
* 
* Description:		This function calculates bias correction element;
*					the calculation depends on mean type - harmonic or arithmetic			
* Input:			None
* Output:			None
*************************************************************************************************/
void Driver::getBiasCorrectionElement() {

	if (_useHarmonicMean) {
		_biasCorrectionElement = 0.7213 / (1 + 1.079 / _bucketsArraySize);
	}
	else {
		_biasCorrectionElement = 0.39701 - (0.41312 / _bucketsArraySize);
	}
}

/*************************************************************************************************
* Function:			establishCommunication()
* 
* Description:		This function establishes socket communication with the Script			
* Input:			None
* Output:			SUCCESS/FAILURE indication
*************************************************************************************************/
Status Driver::establishCommunication() {
	
	struct sockaddr_in scriptSocket, driverSocket;
	memset(&scriptSocket, 0, sizeof(scriptSocket));
	memset(&driverSocket, 0, sizeof(driverSocket));
	// create socket:
	scriptSocket.sin_family = AF_INET;
	scriptSocket.sin_addr.s_addr = inet_addr(SCRIPT_IP);
	scriptSocket.sin_port = htons(SCRIPT_PORT);
	_socket = socket(AF_INET, SOCK_STREAM, 0); // IPPROTO_TCP
	if (_socket < 0) {
		return FAILURE_SOCKET_START_SOCKET;
	}
	// enable address and port reuse:
	int reuse = 1;
	if (setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
		return FAILURE_SOCKET_START_REUSEADDR;
	}
	if (setsockopt(_socket, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(int)) < 0) {
		return FAILURE_SOCKET_START_REUSEPORT;
	}	
	// bind any port number (avoid Python 10054 socket error):
	driverSocket.sin_family = AF_INET;
	driverSocket.sin_addr.s_addr = htonl(INADDR_ANY);
	driverSocket.sin_port = htons(DRIVER_PORT);
	if (bind(_socket, (struct sockaddr*)&driverSocket, sizeof(driverSocket)) < 0) {
		return FAILURE_SOCKET_START_BIND;
	}
	// connect the socket:
	if (connect(_socket, (struct sockaddr*)&scriptSocket, sizeof(scriptSocket)) < 0) {
		return FAILURE_SOCKET_START_CONNECT;
	}
	// signal the script successful connection establishment:
	_socketBuffer[0] = SUCCESS_SOCKET_STARTED;
	if (write(_socket, _socketBuffer, strlen(_socketBuffer)) < 0) {
		return FAILURE_SOCKET_START_WRITE;
	}
	memset(_socketBuffer, 0, sizeof(_socketBuffer));
	return SUCCESS;
}

/*************************************************************************************************
* Function:			checkForData()
* 
* Description:		This function checks for new incoming data;
*					after the Script reads ASIC registers - it signals the Driver that new data is available			
* Input:			None
* Output:			SUCCESS/FAILURE indication
*************************************************************************************************/
Status Driver::checkForData() {
	
	for (;;) {
		int result = read(_socket, _socketBuffer, SOCK_BUF_SZ);
		if (result < 0) {
			usleep(1000);
			continue;
		}
		if (_socketBuffer[0] != SUCCESS_SOCKET_DATA_AVAILABLE) {
			return sendDriverStatus(FAILURE_SOCKET_UNEXPECTED);
		}
		break;
	}
	return SUCCESS;
}

/*************************************************************************************************
* Function:			readAsicRegisters()
* 
* Description:		This function extracts ASIC registers values from ASIC output file			
* Input:			None
* Output:			SUCCESS/FAILURE indication
*************************************************************************************************/
Status Driver::readAsicRegisters() {

	// open ASIC output file:
	fstream asicOutputFileStream;
	asicOutputFileStream.open(_asicOutputFilePath, fstream::in);
	if (!asicOutputFileStream.is_open()) {
		return sendDriverStatus(FAILURE_ASIC_OUTPUT_FILE);
	}
	string asicOutputFileLine;
	vector<string> asicOutputFileLines;
	while (getline(asicOutputFileStream, asicOutputFileLine) != nullptr) {
		asicOutputFileLines.push_back(asicOutputFileLine);
	}
	_meanDataRegister = 0;
	string openingBracket = "[";
	string closingBracket = "]";
	string equalSign = "=";
	if (!_useSampling) {
		for (unsigned int j = 0; j < asicOutputFileLines.size(); j++) {
			asicOutputFileLine = asicOutputFileLines[j];
			if (asicOutputFileLine.find(_meanDataRegisterName + openingBracket) != string::npos) {
				string::size_type equalSignIndex = asicOutputFileLine.find(equalSign) + equalSign.length();
				string registerValueString = asicOutputFileLine.substr(equalSignIndex, string::npos);
				_meanDataRegister = std::stoull(registerValueString);
				break;
			}
		}
	}
	else {
		for (unsigned int i = 0, j = 0; i < _y2uArraySize; i++) {
			for (; j < asicOutputFileLines.size(); j++) {
				bool uDataRegisterRead = false, meanDataRegisterRead = false;
				asicOutputFileLine = asicOutputFileLines[j];
				if (asicOutputFileLine.find(_uDataRegisterName + openingBracket) != string::npos) {
					uDataRegisterRead = true;
				}
				else if (asicOutputFileLine.find(_meanDataRegisterName + openingBracket) != string::npos) {
					meanDataRegisterRead = true;
				}
				else {
					continue;
				}
				string::size_type openingBracketIndex = asicOutputFileLine.find(openingBracket) + openingBracket.length();
				string::size_type closingBracketIndex = asicOutputFileLine.find(closingBracket);
				string registerIndexString = asicOutputFileLine.substr(openingBracketIndex, closingBracketIndex - openingBracketIndex);
				unsigned int registerIndex = (unsigned int)(std::stoi(registerIndexString));
				if (uDataRegisterRead) {
					if (registerIndex != i) {
						asicOutputFileStream.close();
						return sendDriverStatus(FAILURE_ASIC_OUTPUT_DATA); 
					}
				}
				else {
					assert(meanDataRegisterRead);
					assert(registerIndex == 0);
				}
				string::size_type equalSignIndex = asicOutputFileLine.find(equalSign) + equalSign.length();
				string registerValueString = asicOutputFileLine.substr(equalSignIndex, string::npos);
				if (uDataRegisterRead) {
					_uDataRegister[i] = std::stoull(registerValueString);
					j++;
					break;
				}
				else {
					_meanDataRegister = std::stoull(registerValueString);
				}
			}	
		}
	}
	// close ASIC output file:
	asicOutputFileStream.close();
	if (_meanDataRegister == 0) {
		return sendDriverStatus(FAILURE_ASIC_OUTPUT_DATA); 
	}
	return SUCCESS;
}

/*************************************************************************************************
* Function:			estimateMean()
* 
* Description:		This function applies bias correction element to ASIC output mean 			
* Input:			None
* Output:			None
*************************************************************************************************/
void Driver::estimateMean() {

	_meanWindow[_currentTimeSlot] = (unsigned long long)(_biasCorrectionElement * _meanDataRegister);
}

/*************************************************************************************************
* Function:			estimateSingleElementsNumber()
* 
* Description:		This function calculates the number of the single elements in U-merge product;
*					this number is required for sampling bias correction 			
* Input:			None
* Output:			None
*************************************************************************************************/
void Driver::estimateSingleElementsNumber() {
	
	if (!_useSampling) {
		return;
	}
	for (unsigned int i = 0; i < _y2uArraySize; i++) {
		_uWindow[i][_currentTimeSlot] = _uDataRegister[i];
	}
	if (!_isUwindowFull) {
		return;
	}
	unsigned long long* uWindowMin = new unsigned long long[_y2uArraySize];
	unsigned int* uRowIndex = new unsigned int[_slidingWindowSize];	
	memset(uWindowMin, 0, _y2uArraySize * sizeof(unsigned long long));	
	memset(uRowIndex, 0, _slidingWindowSize * sizeof(unsigned int));
	unsigned int uMinRowIndexCurrent = 0, uMinColumnIndexCurrent = 0;
	unsigned long long uMinCurrent = _uWindow[uMinRowIndexCurrent][uMinColumnIndexCurrent];
	for (unsigned int i = 0; i < _y2uArraySize; i++) {
		for (unsigned int j = 0; j < _slidingWindowSize; j++) {
			if (uMinCurrent >= _uWindow[uRowIndex[j]][j]) {	
				uMinCurrent = _uWindow[uRowIndex[j]][j];
				uMinRowIndexCurrent = uRowIndex[j];
				uMinColumnIndexCurrent = j;
			}
		}
		uWindowMin[i] = _uWindow[uMinRowIndexCurrent][uMinColumnIndexCurrent];
		uRowIndex[uMinColumnIndexCurrent]++;
		if (uRowIndex[uMinColumnIndexCurrent] < _y2uArraySize) {
			uMinCurrent = _uWindow[uRowIndex[uMinColumnIndexCurrent]][uMinColumnIndexCurrent];
		}
	}
	unsigned int uCurrentIndex, uPreviousIndex = 0;
	unsigned long long uCurrent, uPrevious = uWindowMin[uPreviousIndex];
	for (unsigned int uCurrentIndex = 1; uCurrentIndex < _y2uArraySize; uCurrentIndex++) {
		uCurrent = uWindowMin[uCurrentIndex];
		if (uCurrent == uPrevious) {
			continue;
		}
		if (uCurrentIndex - uPreviousIndex == 1) {
			_numberOfSingleElementsInUwindow++;
		}
		uPrevious = uCurrent;
		uPreviousIndex = uCurrentIndex;
	}
	uPrevious = uWindowMin[_y2uArraySize - 2];
	if (uCurrent != uPrevious) {
		_numberOfSingleElementsInUwindow++;
	}
	delete[] uWindowMin;
	delete[] uRowIndex;
}

/*************************************************************************************************
* Function:			outputCardinalityEstimation()
* 
* Description:		This function outputs cardinality estimation result to the Script			
* Input:			None
* Output:			SUCCESS/FAILURE indication
*************************************************************************************************/
Status Driver::outputCardinalityEstimation() {

	Status status = SUCCESS;
	if (_useSampling) {
		for (unsigned int i = 0; i < _slidingWindowSize; i++) {	
			_cardinalityEstimation += _meanWindow[i];
		}
		_cardinalityEstimation /= _slidingWindowSize;
	}
	else {
		_cardinalityEstimation = _meanWindow[_currentTimeSlot];
	}
	if (_useSampling) {
		if (_numberOfSingleElementsInUwindow == _y2uArraySize) {
			status = FAILURE_CORRECTION;
		}
		else {
			_cardinalityEstimation /= (1 - (float)_numberOfSingleElementsInUwindow / _y2uArraySize);
		}
	}
	if (status == FAILURE_CORRECTION) {
		sendDriverStatus(status);
	}
	else {
		sendDriverOutput();
	}
	if ((_currentTimeSlot + 1) == _slidingWindowSize) {
		_isUwindowFull = true;
	}
	_currentTimeSlot = (_currentTimeSlot + 1) % _slidingWindowSize;
	_numberOfSingleElementsInUwindow = 0;
	_cardinalityEstimation = 0;
	return status;
}

/*************************************************************************************************
* Function:			sendDriverStatus()
* 
* Description:		This function sends Driver status to the Script		
* Input:			None
* Output:			SUCCESS/FAILURE indication
*************************************************************************************************/
Status Driver::sendDriverStatus(Status status) {
	
	_socketBuffer[0] = status;
	for (int i = 0; i < 3; i++) {
		if (write(_socket, _socketBuffer, strlen(_socketBuffer)) < 0) {
			status = FAILURE_SOCKET_WRITE;
			usleep(1000);
			continue;
		}
		break;
	}
	memset(_socketBuffer, 0, sizeof(_socketBuffer));
	return status;
}

/*************************************************************************************************
* Function:			sendDriverOutput()
* 
* Description:		This function sends Driver output to the Script				
* Input:			None
* Output:			SUCCESS/FAILURE indication
*************************************************************************************************/
Status Driver::sendDriverOutput() {
	
	if ((_useSampling == true) && (_isUwindowFull == false)) {
		return sendDriverStatus(SUCCESS_SOCKET_DATA_NOT_READY);
	}
	_socketBuffer[0] = SUCCESS_SOCKET_DATA_READY;
	sprintf(_socketBuffer + 1, "%llu", _cardinalityEstimation);
	if (write(_socket, _socketBuffer, sizeof(_socketBuffer)) < 0) {
		return sendDriverStatus(FAILURE_SOCKET_WRITE);
	}
	memset(_socketBuffer, 0, sizeof(_socketBuffer));
}
