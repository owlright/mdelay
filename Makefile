all: timestamping sender master slave

BIN_DIR := bin

timestamping: rx_timestamping.c util.o $(BIN_DIR)
	gcc -O2 rx_timestamping.c util.o -o $(BIN_DIR)/timestamping
sender: sender.c util.o $(BIN_DIR)
	gcc -O2 sender.c util.o -o $(BIN_DIR)/sender -lpthread

util.o: util.c
	gcc -c util.c -o util.o

run: timestamping
	sudo ./$(BIN_DIR)/timestamping --port 1337 --max 100

send: sender
	sudo ./$(BIN_DIR)/sender -i enp114s0 --dport 1337 --max 100

slave: slave.c util.c
	gcc -O2 slave.c util.c -o $(BIN_DIR)/slave

master: master.c util.c
	gcc -O2 master.c util.c -o $(BIN_DIR)/master -lpthread

tai: tool/set_tai_offset.c
	gcc $^ -o $(BIN_DIR)/$@

debug_server: tool/debug_server.c util.o
	gcc -O2 $^ -o $(BIN_DIR)/$@

run_debug_server: debug_server
	./$(BIN_DIR)/debug_server