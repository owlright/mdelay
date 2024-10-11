all: timestamping sender

timestamping: rx_timestamping.c
	gcc -O2 rx_timestamping.c -o timestamping
sender: sender.c
	gcc -O2 sender.c -o sender

run: timestamping
	sudo ./timestamping --port 1337 --max 100000
