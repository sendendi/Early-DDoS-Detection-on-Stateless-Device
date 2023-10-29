#!/usr/bin/python

import argparse
import subprocess
import sys
import threading
import random
import socket
import struct
import psutil
import matplotlib.dates as dt
import matplotlib.pyplot as plt
import logging
logging.getLogger("scapy.runtime").setLevel(logging.ERROR)
from scapy.all import *
from datetime import datetime
from time import sleep, time
from math import ceil

from mininet.net import Mininet
from mininet.topo import Topo
from mininet.log import setLogLevel, info
from p4_mininet import P4Switch, P4Host

BMV2_PATH = "/home/victor/Downloads/bmv2/"
P4C_BMV2_PATH = "/home/victor/Downloads/p4c-bmv2/"
P4C_BMV2_SCRIPT_PATH = P4C_BMV2_PATH + "p4c_bm/__main__.py"
RUNTIME_CLI_PATH = BMV2_PATH + "tools/runtime_CLI.py"
SWITCH_PATH = BMV2_PATH + "targets/simple_switch/simple_switch"
P4SRC_PATH = BMV2_PATH + "targets/ddos_switch/ddos_switch.p4"
P4SRC_OUTPUT_PATH = BMV2_PATH + "targets/ddos_switch/ddos_switch_output.txt"
JSON_PATH = BMV2_PATH + "targets/ddos_switch/ddos_switch.json"
DRIVER_PATH = BMV2_PATH + "targets/ddos_switch/driver"

NEW_LINE = "\n"
SOCK_BUF_SZ = 64
STATUS_CODE = 0
STATUS_MSG = 1

status = {	"SUCCESS_SOCKET_STARTED" :
			("\x01", "Driver succeeded to establish socket communication with the script"),
			"SUCCESS_SOCKET_DATA_AVAILABLE" :
			("\x02", "ASIC output is ready for the driver to process"),
			"SUCCESS_SOCKET_DATA_READY" :
			("\x03", "Driver cardinality estimation result is ready"),
			"SUCCESS_SOCKET_DATA_NOT_READY" :
			("\x04", "Estimated cardinality is not available until the first sliding window cycle is completed"),
			"FAILURE_SOCKET_UNEXPECTED" :
			("\x05", "Driver received unexpected input while waiting for ASIC output"),
			"FAILURE_ASIC_SOURCE_FILE" :
			("\x06", "Driver failed to open ASIC source file"),
			"FAILURE_ASIC_OUTPUT_FILE" :
			("\x07", "Driver failed to open ASIC output file"),
			"FAILURE_ASIC_OUTPUT_DATA" :
			("\x08", "Driver failed to read mean or U registers data"),
			"FAILURE_CORRECTION" :
			("\x09", "Driver failed to calculate sampling correction element - division by zero, setting cardinality estimation to maximum: "),
			"FAILURE_SOCKET_START_SOCKET" :
			("\x0a", "Driver failed to call socket()"),
			"FAILURE_SOCKET_START_REUSEADDR" :
			("\x0b", "Driver failed to set SO_REUSEADDR socket option"),
			"FAILURE_SOCKET_START_REUSEPORT" :
			("\x0c", "Driver failed to set SO_REUSEPORT socket option"),
			"FAILURE_SOCKET_START_BIND" :
			("\x0d", "Driver failed to bind() the socket"),
			"FAILURE_SOCKET_START_CONNECT" :
			("\x0e", "Driver failed to connect() the socket"),
			"FAILURE_SOCKET_START_WRITE" :
			("\x0f", "Driver failed to signal the script successful socket communication establishment"),
			"FAILURE_SOCKET_WRITE" :
			("\x10", "Socket write failed"),
			"FAILURE_SOCKET_READ" :
			("\x11", "Socket read failed"),
			"STATUS_MAX" :
			("\x12", "") }

args = None
net = None
driver_con = None
script_socket = None
script_ip = "localhost"
script_port = 50007
traffic_socket = None
traffic_ip = "veth0"
traffic_port = 50007
read_reg_cmd = ""
write_reg_cmd = ""
estimated_cardinality = 0
actual_cardinality_set = set()
exit_socket = False
exit_driver = False
exit_mininet = False
exit_cli = False
exit_traffic = False
data_available = False
data_ready = False
data_reset = False
run_driver_routine = True
run_traffic_routine = True
apply_heavy_traffic = False

class SingleSwitchTopo(Topo):
	def __init__(self, **opts):
		Topo.__init__(self, **opts)
		# start Mininet with a host and a single switch: 
		switch = self.addSwitch('s1', 
						sw_path = SWITCH_PATH,
						json_path = JSON_PATH,
						pcap_dump = True)
		host = self.addHost('h1')
		self.addLink(host, switch)

class DynamicDisplay():
	def __init__(self):
		global args
		# set interactive mode on:
		plt.ion()	
		# set up plot:
		self.fig = plt.figure(figsize = (5, 5))
		self.line_act, = plt.plot_date([], [], '-rv')
		self.line_est, = plt.plot_date([], [], '-b^')
		plt.grid()
		# set up axes:
		self.ax = plt.gca()
		self.ax.xaxis.set_major_formatter(dt.DateFormatter("%H:%M:%S"))
		# set up text:
		plt.rc('font', size = 10)
		plt.xlabel('time')
		plt.ylabel('cardinality')
		self.fig.subplots_adjust(left = 0.15)
		self.fig.legend((self.line_act, self.line_est), ('actual', 'estimated'), bbox_to_anchor = (0, 0, 1, 1), loc = 'upper right', fancybox = True)
		if args.use_sampling:
			args_text = (' h = %i\n m = %i\n y = %i\n u = %i\n w = %i\n a = %i s' % (args.use_harmonic_mean, args.buckets_array_size, args.x2y_sampling_size, args.y2u_array_size, args.sliding_window_size, args.asic_sampling_time))
		else:
			args_text = (' h = %i\n m = %i\n a = %i s' % (args.use_harmonic_mean, args.buckets_array_size, args.asic_sampling_time))
		props = dict(boxstyle = 'round', facecolor = 'yellow', alpha = 0.5)
		self.ax.text(0.05, 0.95, args_text, transform = self.ax.transAxes, fontsize = 10, verticalalignment = 'top', bbox = props)			
		
	def display(self, time_list, actual_cardinality_list, estimated_cardinality_list):
		# convert the time to matplotlib format:
		time_list = dt.date2num(time_list)
		# update actual cardinality data:
		self.line_act.set_data(time_list, actual_cardinality_list)
		# update estimated cardinality data:
		self.line_est.set_data(time_list, estimated_cardinality_list)
		# format the time:
		self.fig.autofmt_xdate()
		# rescale:
		self.ax.relim()
		self.ax.autoscale_view()
		# draw and flush:
		self.fig.canvas.draw()
		self.fig.canvas.flush_events()
		
def parse_params():
	global args
	parser = argparse.ArgumentParser(description = 'Early DDoS demo')
	parser.add_argument('--use-sampling', help = 'Set 1/0 to sample the traffic or not', type = int, default = 1, action = "store", required = False)
	parser.add_argument('--use-harmonic-mean', help = 'Use 1 to apply harmonic mean; use 0 to apply arithmetic mean', type = int, default = 1, action = "store", required = False)
	parser.add_argument('--sliding-window-size', help = 'Specify the number of the measurements that sampling bias correction element is based on', type = int, default = 3, action = "store", required = False)
	parser.add_argument('--buckets-array-size', help = 'Specify the number of the buckets that are used for mean calculation', type = int, default = 8, action = "store", required = False)
	parser.add_argument('--y2u-array-size', help = 'Specify the size of U array that Y packets are subsampled to', type = int, default = 8, action = "store", required = False)
	parser.add_argument('--x2y-sampling-size', help = 'Specify the size of X batch that Y packets are sampled from; should be power of 2', type = int, default = 128, action = "store", required = False)
	parser.add_argument('--asic-sampling-time', help = 'Specify how long the ASIC runs before its output is read, in [s]', type = int, default = 10, action = "store", required = False)
	parser.add_argument('--display-time', help = 'Specify how often cardinality estimation is displayed (if available), in [s]', type = int, default = 20, action = "store", required = False)
	args = parser.parse_args()
	params_ok = ((args.x2y_sampling_size & (args.x2y_sampling_size - 1)) == 0)
	if not params_ok:
		print "X-to-Y sampling size should be power of 2"
	else:
		if args.use_sampling:
			params = ["[*] Starting Early DDoS demo with the following parameters:", NEW_LINE, "\t",
						"- X-to-Y sampling size: ", str(args.x2y_sampling_size), NEW_LINE, "\t",
						"- Y-to-U array size: ", str(args.y2u_array_size), NEW_LINE, "\t",
						"- sliding window size: ", str(args.sliding_window_size), NEW_LINE, "\t",
						"- use harmonic mean: ", str(args.use_harmonic_mean), NEW_LINE, "\t",
						"- buckets array size: ", str(args.buckets_array_size), NEW_LINE, "\t",
						"- ASIC sampling time: ", str(args.asic_sampling_time), "s", NEW_LINE, "\t",
						"- display time: ", str(args.display_time), "s", NEW_LINE]
		else:
			params = ["[*] Starting Early DDoS demo with the following parameters:", NEW_LINE, "\t",
						"- use harmonic mean: ", str(args.use_harmonic_mean), NEW_LINE, "\t",
						"- buckets array size: ", str(args.buckets_array_size), NEW_LINE, "\t",
						"- ASIC sampling time: ", str(args.asic_sampling_time), "s", NEW_LINE, "\t",
						"- display time: ", str(args.display_time), "s", NEW_LINE, "\t",
						"Other parameters are ignored since traffic sampling is not used", NEW_LINE]
		print " ".join(params)
	sleep(2)
	return params_ok

def start_socket():
	print "[*] Starting socket communication" + NEW_LINE
	global exit_socket
	global script_socket
	global driver_con
	script_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	script_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	script_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
	try:
		script_socket.bind((script_ip, script_port))
		script_socket.listen(1);
		# start driver thread:
		driver_ok = start_driver()
		if driver_ok:
			driver_con = script_socket.accept()[0]
			data = driver_con.recv(SOCK_BUF_SZ)
			if data:
				# driver socket is successfully initialized:
				if data[0] == status["SUCCESS_SOCKET_STARTED"][STATUS_CODE]:
					return True
		return False
	except socket.error as msg:
		print "Script socket start error code: " + str(msg[0]) + ", message: " + msg[1] + NEW_LINE
	except Exception as e:
		print "Error message: %s" % e + NEW_LINE
		return False
	finally:
		exit_socket = True
		sleep(2)
		
def stop_socket():
	if exit_socket:
		print "- Stopping socket communication"
		global script_socket
		global driver_con
		try:
			# stop driver thread:
			stop_driver()
			# close script and driver sockets:
			if driver_con:
				driver_con.shutdown(1)
				driver_con.close()
			script_socket.shutdown(1)
			script_socket.close()
		except:
			return			

def driver_routine():
	global args
	global data_ready
	global data_reset
	global data_available
	global estimated_cardinality
	global run_driver_routine
	cmd = [DRIVER_PATH, str(args.use_sampling), str(args.use_harmonic_mean), str(args.sliding_window_size),
			str(args.buckets_array_size), str(args.y2u_array_size), str(args.x2y_sampling_size)]
	print " ".join(cmd) + NEW_LINE
	proc = subprocess.Popen(cmd, stderr = subprocess.PIPE, stdout = subprocess.PIPE)
	sleep(4)
	if proc.poll() is not None:
		print "Driver module premature exit status - stdout: " + proc.communicate()[0] + ", stderr: " + proc.communicate()[1] + NEW_LINE
		return
	estimated_cardinality_max = 0;	
	while run_driver_routine:
		if data_available:	
			# signal the driver data is available:
			driver_con.send(status["SUCCESS_SOCKET_DATA_AVAILABLE"][STATUS_CODE])			
			data_available = False
			# wait for driver output:
			msg = ""
			data = driver_con.recv(SOCK_BUF_SZ)	
			if data:
				# expecting "data ready" status from the driver:
				if data[0] == status["SUCCESS_SOCKET_DATA_READY"][STATUS_CODE]:
					msg += data
					idx = data.find('\0');
					# read driver output:
					while idx == -1:
						data = driver_con.recv(SOCK_BUF_SZ)
						if not data:
							print "\t- Error: failed to read driver output"
							data_reset = True
							break
						msg += data
						idx = data.find('\0');
					if idx == 1:
						print "\t- Error: empty driver output"
						data_reset = True
						break
					else:
						estimated_cardinality = int(msg[1 : idx])
						if estimated_cardinality > estimated_cardinality_max:
							estimated_cardinality_max = estimated_cardinality
						print "\t- Estimated cardinality is: " + str(estimated_cardinality)
						data_ready = True
				# handle other status received from the driver: 
				elif data[0] < status["STATUS_MAX"][STATUS_CODE]:
					# driver fails to calculate correction element when number of single elements equals U size:
					if data[0] == status["FAILURE_CORRECTION"][STATUS_CODE]:
						print "\t- " + status[key][STATUS_MSG] + str(estimated_cardinality_max)
						estimated_cardinality = estimated_cardinality_max
						data_ready = True
					else:
						for key in status.keys():
							if data[0] == status[key][STATUS_CODE]:
								print "\t- " + status[key][STATUS_MSG]
						data_reset = True		
				# unknown status received:			
				else:
					print "\t- Error: unknown status code received"
					data_reset = True
		else:
			sleep(0.01)
	
def start_driver():
	print "[*] Starting driver module:"
	global exit_driver
	driver_thread = threading.Thread(target = driver_routine)
	driver_thread.daemon = True # enable CTRL-C termination
	driver_thread.start()
	driver_thread.join(4)
	# no premature exit - driver is successfully launched:
	if driver_thread.is_alive():
		exit_driver = True
		return True	
	return False

def stop_driver():
	if exit_driver:
		print "- Stopping driver module" + NEW_LINE
		global run_driver_routine
		run_driver_routine = False
		sleep(2)
		try:
			for proc in psutil.process_iter():
				# terminate driver process:
				if proc.name() == "driver":
					proc.kill()
					break
		except Exception as e:
			print "Error message: %s" % e + NEW_LINE					
		sleep(2)
		
def compile_asic():
	print "[*] Compiling ASIC source code:"
	cmd = [P4C_BMV2_SCRIPT_PATH, "--json", JSON_PATH, P4SRC_PATH]
	print " ".join(cmd)
	proc = subprocess.Popen(cmd, stdout = subprocess.PIPE)
	sleep(2)
	# check compilation status:
	res = proc.communicate()[0]
	success_str = "Generating json output"
	if success_str in res:
		print NEW_LINE
		return True
	return False
		
def start_mininet():
	print "[*] Starting Mininet:"
	global exit_mininet
	global net
	setLogLevel('info') # enable Mininet logging
	topo = SingleSwitchTopo()
	net = Mininet(topo = topo,
				host = P4Host,
				switch = P4Switch,
				controller = None)
	net.start()
	host = net.get('h1')
	host.config() # disable IPv6
	exit_mininet = True
	sleep(2)
	
def stop_mininet():
	if exit_mininet:
		print "- Stopping Mininet:"
		global net
		net.stop()
		sleep(2)
	
def start_runtime_cli():
	print "[*] Starting runtime CLI:"
	global args
	global exit_cli
	global read_reg_cmd
	global write_reg_cmd	
	cmd = [RUNTIME_CLI_PATH, "--json", JSON_PATH]
	print " ".join(cmd)
	# configure default table actions (to be applied on table miss):
	config_cmd = "table_set_default get_cardinality get_cardinality_action" + NEW_LINE + \
				"table_set_default drop_packet drop_packet_action" + NEW_LINE
	if args.use_sampling:
		config_cmd = config_cmd + "table_set_default sample_x2y sample_x2y_action" + NEW_LINE + \
					"table_set_default reset_sample_x2y reset_sample_x2y_action" + NEW_LINE + \
					"table_set_default sample_y2u sample_y2u_action" + NEW_LINE
	proc = subprocess.Popen(cmd, stdin = subprocess.PIPE, stdout = subprocess.PIPE)
	success_str = "Control utility for runtime P4 table manipulation"
	res = proc.communicate(input = config_cmd)[0]
	if not success_str in res:
		return False
	# generate read registers commands string:
	read_reg_cmd = "register_read cardinality_mean_register 0" + NEW_LINE
	if args.use_sampling:
		for n in range(0, args.y2u_array_size):
			read_reg_cmd = read_reg_cmd + ("register_read sample_y2u_data_register %d" % n) + NEW_LINE
	# generate write registers commands string:
	write_reg_cmd = "register_write cardinality_mean_register 0 0" + NEW_LINE
	for n in range(0, args.buckets_array_size):
		write_reg_cmd = write_reg_cmd + ("register_write cardinality_buckets_register %d 0" % n) + NEW_LINE
	if args.use_sampling:
		write_reg_cmd = write_reg_cmd + "register_write sample_y2u_index_register 0 0" + NEW_LINE
		for n in range(0, args.y2u_array_size):
			write_reg_cmd = write_reg_cmd + ("register_write sample_y2u_data_register %d 0" % n) + NEW_LINE
	exit_cli = True		
	sleep(2)
	print NEW_LINE
	return True
		
def stop_runtime_cli():
	if exit_cli:
		print "- Stopping runtime CLI"
		cmd = "redis-cli FLUSHALL"
		subprocess.Popen(cmd, stdout = subprocess.PIPE, shell = True)
		sleep(2)
		
def read_runtime_cli():
	global read_reg_cmd
	output = open(P4SRC_OUTPUT_PATH, 'w')
	cmd = [RUNTIME_CLI_PATH, "--json", JSON_PATH]
	# use file for output:
	proc = subprocess.Popen(cmd, stdin = subprocess.PIPE, stdout = output)
	# send read registers command:
	proc.communicate(input = read_reg_cmd)
	output.flush()
	output.close()
	sleep(0.5)
	# for debug only:
	with open(P4SRC_OUTPUT_PATH, 'r') as output:
		for line in output:
			print line,
		print NEW_LINE
	
def write_runtime_cli():
	global write_reg_cmd
	cmd = [RUNTIME_CLI_PATH, "--json", JSON_PATH]
	proc = subprocess.Popen(cmd, stdin = subprocess.PIPE, stdout = subprocess.PIPE)
	# send write registers command:
	proc.communicate(input = write_reg_cmd)
	
def traffic_routine():
	global traffic_socket
	global run_traffic_routine
	global apply_heavy_traffic
	global actual_cardinality_set
	while run_traffic_routine:
		try:
			# generate the packet:
			if apply_heavy_traffic:
				ip_src = "192.168.48.%i" % random.randint(10, 20)
				ip_dst = "%i.15.67.19" % random.choice([5, 6, 7, 110, 210])
				port_src = random.randint(36000, 36004)
				port_dst = random.randint(8999, 9010)
			else:
				ip_src = "192.168.48.137"
				ip_dst = "%i.15.67.19" % random.choice([10, 60, 110, 160, 210])
				port_src = 5555
				port_dst = random.randint(8999, 9020)
			transport = random.choice([UDP, TCP])
			packet = Ether() / IP(src = ip_src, dst = ip_dst) / transport(sport = port_src, dport = port_dst)
			# send the packet:
			traffic_socket.send(str(packet))
			# insert hash packet value into actual cardinality set:
			ip_src = struct.unpack("!I", socket.inet_aton(str(packet[IP].src)))[0]
			ip_dst = struct.unpack("!I", socket.inet_aton(str(packet[IP].dst)))[0]
			hash_value = (ip_dst * 59) ^ ip_src ^ (port_src << 16) ^ port_dst
			actual_cardinality_set.add(hash_value)
		except (KeyboardInterrupt, Exception):
			break		
			
def start_traffic():
	print "[*] Starting traffic generation" + NEW_LINE
	global traffic_socket
	global exit_traffic
	# start traffic socket (scapy's sendp() is slow):
	traffic_socket = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x03))
	traffic_socket.bind((traffic_ip, traffic_port))
	# start traffic thread:
	traffic_thread = threading.Thread(target = traffic_routine)
	traffic_thread.daemon = True # enable CTRL-C termination
	traffic_thread.start()
	exit_traffic = True
	
def stop_traffic():
	if exit_traffic:
		print "- Stopping traffic generation"
		global traffic_socket
		global run_traffic_routine
		# stop traffic socket:
		traffic_socket.close()
		# stop traffic thread:
		run_traffic_routine = False
		sleep(2)

def main_loop():
	global args
	global data_ready
	global data_reset
	global data_available
	global apply_heavy_traffic
	global estimated_cardinality
	global actual_cardinality_set
	time_list = []
	actual_cardinality_list = []
	estimated_cardinality_list = []
	plotter = DynamicDisplay()
	display_times = ceil(float(args.display_time) / args.asic_sampling_time)
	display_len_max = 4 * args.sliding_window_size
	traffic_counter = 0;
	traffic_counter_max = 3;
	while True:
		try:
			# toggle traffic intensity:
			if traffic_counter == traffic_counter_max:
				if apply_heavy_traffic:
					traffic_counter = 0
					apply_heavy_traffic = False
				else:
					traffic_counter = traffic_counter_max - 1
					apply_heavy_traffic = True					
			traffic_counter += 1
			display_timer = display_times
			# display timer loop:
			while display_timer > 0:
				ref_time = time()
				asic_timer = args.asic_sampling_time
				# ASIC timer loop:
				while asic_timer > 0:
					asic_timer -= (time() - ref_time)
					ref_time = time()
					sleep(0.01)
				# update display timer:	
				display_timer -= 1
				# sample the ASIC:
				read_runtime_cli()
				write_runtime_cli()
				if apply_heavy_traffic:
					print "\t- Heavy traffic"
				else:
					print "\t- Normal traffic"				
				# signal driver thread data is available:
				data_available = True		
				while data_available:
					sleep(0.01)					
				# wait for the driver thread to receive driver output:
				while not data_ready:
					if data_reset:
						break
					sleep(0.01)
				# insert driver output into estimated cardinality list:		
				if data_ready:
					estimated_cardinality_list.append(estimated_cardinality)
					# update actual cardinality	list:
					actual_cardinality_list.append(len(actual_cardinality_set))
					print "\t- Actual cardinality is: " + str(len(actual_cardinality_set))
					# update time list:
					time_list.append(datetime.now())
					# remove old data from display lists:
					while len(time_list) > display_len_max:
						time_list.pop(0)
						actual_cardinality_list.pop(0)
						estimated_cardinality_list.pop(0)
				# reset global variables:
				actual_cardinality_set.clear()		
				estimated_cardinality = 0
				data_reset = False
				data_ready = False
				print NEW_LINE
			# display the results (if available):
			if time_list:
				plotter.display(time_list, actual_cardinality_list, estimated_cardinality_list)	
		except (KeyboardInterrupt, Exception):	
			return
		
def exit_main():
	print NEW_LINE + "[*] Exiting Early DDoS demo:"
	stop_traffic()
	stop_runtime_cli()
	stop_mininet()
	stop_socket()
	sys.exit()
				
def main():
	try:
		params_ok = parse_params()
		if params_ok:
			socket_ok = start_socket()
			if socket_ok:
				asic_ok = compile_asic()
				if asic_ok:
					start_mininet()
					cli_ok = start_runtime_cli()
					if cli_ok:
						start_traffic()
						main_loop()
	finally:				
		exit_main()	

if __name__ == '__main__':
    main()
    
