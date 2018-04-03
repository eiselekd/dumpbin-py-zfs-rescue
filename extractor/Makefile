
all: extract.exe

extract.exe: main.o objset.o zap.o zio.o dnode.o dump.o lz4.o
	gcc -g -std=c++14 $^ $(ADDLIB) -lstdc++ -o $@

%.o: %.cpp
	@-mkdir -p $(dir $@)
	gcc -g -std=c++14 -I$(CURDIR)/include -I$(CURDIR)/include_libspl/include -c $< -o $@

clean:
	rm -f *.o *.exe

test:
	./extract.exe -c ../disk0.bin:../disk1.bin:../disk2.bin 0:5a000:2000 test.bin
