all:

prepare:
	sudo apt-get install python3-lz4

server:
	cd block_server; sudo python3 server.py
