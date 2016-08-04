# Early DDoS Detection on Stateless Device - Project Files

## Before you begin:

- please pay attention to Ubuntu version that is currently recommended for P4 language environment (Ubuntu 14.04 qualifies for April 2016 environment)
- install python pip:
	sudo apt-get install python-pip

## Installing P4 environment:

1. clone the following P4 language Github repositories:
- git clone https://github.com/p4lang/p4c-bm.git p4c-bmv2
	p4c-bmv2 repository is the compiler for the behavioural model -
	it generates a JSON file from P4 source code
- git clone https://github.com/p4lang/behavioral-model.git bmv2
	bmv2 repository is the second version of the behavioural model -
	it is a C++ software switch that is configured by the compiler output file  

2. install p4c-bmv2 including additional dependencies:
	cd to p4c-bmv2 folder
	sudo pip install -r requirements.txt
	sudo python setup.py install
	check the installation with: p4c-bmv2 -h
	
3. install bmv2 including additional dependencies:
	cd to bmv2 folder
	sudo ./install_deps.sh
	./autogen.sh
	./configure
	make
	
4. install mininet, scapy, matplotlib and thrift (>= 0.9.2):
	sudo apt-get install mininet
	sudo pip install scapy thrift matplotlib
	
## Running DDoS switch:

1. create ddos_switch folder in bmv2->targets; this folder should contain:
- the following P4 folder files:
	ddos_switch.p4
	includes sub-folder with:
		parser.p4
		headers.p4
- the following Driver folder files:
	driver.h
	driver.cpp
- the following Python folder files:
	script.py
	p4_mininet.py
- the following Environment folder file:
	veth_setup.sh

2. additional P4 folder files:
	- primitives.json - this file contains the prototypes of DDoS switch primitive actions
		copy primitives.json to p4c-bmv2->p4c_bm (override existing primitives.json)
		re-install p4c-bmv2:
			cd to p4c-bmv2 folder
			sudo python setup.py install
		
	- primitives.cpp - this file contains the implementation of DDoS switch primitive actions
		copy primitives.cpp to bmv2->targets->simple_switch (override existing primitives.cpp)
		cd to bmv2 folder and call make
	
3. run Virtual Ethernet setup script:
	cd to ddos_switch folder
	sudo ./veth_setup.sh
	
4. compile Driver files:
	cd to ddos_switch folder
	g++ driver.cpp -o driver -std=c++11
	
5. check the paths that are listed in the beginning of script.py file:
	BMV2_PATH and P4C_BMV2_PATH should be changed according to p4c-bmv2 and bmv2 clone locations

6. p4_mininet.py is P4 mininet wrapper file that can be changed according to new requirements;
	customize the path in P4Switch->start function
	
7. run DDoS switch:
	cd to ddos_switch folder
	sudo python script.py
	
	to get Script input options use:
		sudo python script.py -h
		
	to terminate the Script use:
		CTRL-C
	
	runtime-generated ddos_switch folder files:
	- ddos_switch.json file is generated when the Script compiles P4 source code
	- ddos_switch_output.txt contains ASIC registers values for Driver input
	- veth1.pcap file contains the traffic log
	- p4s.s1.log contains thrift server output (for debug purposes: uncomment --log-console option
	in P4Switch->start function of p4_mininet.py file to see the actions that are applied by DDoS switch)
