all:

prepare:
	sudo apt-get install python3-lz4

prepare-llfuse:
	wget https://bitbucket.org/nikratio/python-llfuse/downloads/llfuse-1.3.3.tar.bz2
	tar xvf llfuse-1.3.3.tar.bz2
	cd llfuse-1.3.3; python3 setup.py build; sudo python3 setup.py install

server:
	cd block_server; sudo python3 server.py

gen-test:
	-sudo zpool export datapool
	-sudo bash gen1.sh
	for i in `seq 0 2`; do \
		dd if=/dev/zero of=disk$${i}.bin bs=1024 count=$$((1024*64)); \
	done
	sudo bash gen0.sh
	sudo bash gen2.sh
	sudo zpool export datapool
	sudo bash gen1.sh

gen-test:
	-sudo zpool export datapoolone
	-sudo bash genone_1.sh
	dd if=/dev/zero of=diskone0.bin bs=1024 count=$$((1024*64));
	done
	sudo bash genone_0.sh
	sudo bash genone_2.sh
	sudo zpool export datapoolone
	sudo bash genone_1.sh


unzip:
	for i in `seq 0 2`; do \
		bunzip2 -kf disk$${i}.bin.bz2; \
	done

dump:
	sudo zdb -ddddddd datapool > datapool.dump.txt

de:
	gcc -g de.c -o de.exe


re:
	python3 zfs_rescue.py
