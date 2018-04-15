all:

prepare:
	sudo apt-get install python3-lz4

prepare-llfuse:
	wget https://bitbucket.org/nikratio/python-llfuse/downloads/llfuse-1.3.3.tar.bz2
	tar xvf llfuse-1.3.3.tar.bz2
	cd llfuse-1.3.3; python3 setup.py build; sudo python3 setup.py install

server:
	cd block_server; sudo python3 server.py

de:
	gcc -g de.c -o de.exe


re:
	python3 zfs_rescue.py
