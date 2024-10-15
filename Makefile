all: timestamping sender

timestamping: rx_timestamping.c util.o
	gcc -O2 rx_timestamping.c util.o -o timestamping
sender: sender.c util.o
	gcc -O2 sender.c util.o -o sender -lpthread

util.o: util.c
	gcc -c util.c -o util.o

run: timestamping
	sudo ./timestamping --port 1337 --max 100

send: sender
	sudo ./sender -i enp114s0 --dport 1337 --max 100