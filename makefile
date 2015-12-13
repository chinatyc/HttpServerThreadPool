

httpServer:httpServer.o http_conn.o
	g++ -g -pthread http_conn.o httpServer.o -o httpServer

http_conn.o:http_conn.cpp
	g++ -g -c http_conn.cpp

httpServer.o:httpServer.cpp
	g++ -g -c httpServer.cpp

clean:
	rm *.o httpServer
