#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define MAXBUF 53000


// To store CSP
typedef struct {
	char ProtocolPhase;
	char MeasurementType[4];
	int NumberOfProbes;
	int MessageSize;
	double ServerDelay;
	struct timespec delay;
} CSP;

// To store MP
typedef struct {
	char ProtocolPhase;
	int ProbeSequenceNumber;
	char *Payload;
} MP;


// Receive until delimiter is found
int recvUntil( int sockfd, char *received, char delimiter ){

	// Empty Str
	char temp[MAXBUF];
	memset(temp, 0, MAXBUF);

	// Empty receiving buffer
	memset(received, 0, MAXBUF);

	// Receive data
	while( recv(sockfd, temp, MAXBUF, 0) != -1 ){

		// Append data
		strcat(received, temp);

		// Break on new line
		if( temp[strlen(temp)-1] == delimiter ){ break; }

		// Empty temp
		memset(temp, 0, MAXBUF);
	}

	return strlen(received);
}

// Setup server at given port
int setupServer( int hostPort ){

	// Socket
	int socketDescriptor;

	// Check if socket descriptor sucessfully referenced
	if( (socketDescriptor =	socket(AF_INET, SOCK_STREAM, 0)) == -1 ){
		printf("Error creating a socket");
		exit(0);
	}

	// Server Address
	struct sockaddr_in address;
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons(hostPort);

	// Bind to Port
	if ( bind(socketDescriptor, (struct sockaddr*)&address, sizeof(address)) != 0 ){
		printf("Error binding socket descriptor to address");
		exit(0);
	}

	// Start listening for connections
	if( listen(socketDescriptor, 5) != 0 ){
		printf("Error listening on socket");
		exit(0);
	}

	return socketDescriptor;
}


int waitforCSP( int clientfd, char *received, CSP *csp ){

	// Buffer for response
	char *response;

	/* VALIDATE CSP*/
	if(
		// Receive CSP
		recv(clientfd, received, MAXBUF, 0) != -1 &&

		// Parse CSP
		sscanf( received, "%c %s %d %d %lf\n", &csp->ProtocolPhase, csp->MeasurementType, &csp->NumberOfProbes, &csp->MessageSize, &csp->ServerDelay ) == 5 &&

		// Validate Protocol Phase
		csp->ProtocolPhase == 's' &&

		// Validate Measurement Type
		( strcmp(csp->MeasurementType, "rtt") == 0 || strcmp(csp->MeasurementType, "tput") == 0 ) 
	){
		// Successfully Parsed
		response = "200 OK: Ready\n";

		// Validate Delay
		csp->delay.tv_sec = (int) csp->ServerDelay;
		csp->delay.tv_nsec = (csp->ServerDelay-((int)csp->ServerDelay))*1000000000;

		// Respond to CSP
		send(clientfd, response, strlen(response), 0);

		// Print
		printf("\t<--CSP--\n");
		printf("\t\tProtocol Phase: %c\n", csp->ProtocolPhase);
		printf("\t\tMeasurement Type: %s\n", csp->MeasurementType);
		printf("\t\tNumber of Probes: %d\n", csp->NumberOfProbes);
		printf("\t\tMessage Size: %d bytes\n", csp->MessageSize);
		printf("\t\tServer Delay: %f s\n", csp->ServerDelay);
		printf("\t--CSP-->\n");

		return 1;
	}else{

		// Error Parsing
		response = "404 ERROR: Invalid Connection Setup Message\n";

		// Respond with CSP Error
		send(clientfd, response, strlen(response), 0);

		// Close connection
		close(clientfd);

		printf("\t<--CSP--\n\t\tFailed: Closing connection\n\t--CSP-->\n");
		return 0;
	}
}

int waitforMPs( int clientfd, char *received, CSP csp ){

	// Buffer for response
	char *response;

	// MPs
	MP *mps;
	int mpCount = 1;

	// MPs Array
	mps = malloc( sizeof(*mps) * csp.NumberOfProbes );

	// Wait for MPs
	while( mpCount <= csp.NumberOfProbes && mpCount != 0 ){
		printf("\t<--MP--\n\t\tExpecting probe: %d/%d\n", mpCount, csp.NumberOfProbes);

		// Locate MP
		MP mp = mps[mpCount-1];

		// Allocate Message Size
		mp.Payload = malloc(csp.MessageSize+1);

		if(
			// Receive
			recvUntil(clientfd, received, '\n') &&

			// Parse
			sscanf( received, "%c %d %s\n", &mp.ProtocolPhase, &mp.ProbeSequenceNumber, mp.Payload) == 3 &&

			// Protocol Phase
			mp.ProtocolPhase == 'm' &&

			// Probe Sequence Number
			mp.ProbeSequenceNumber == mpCount &&

			// Truncate Extra Data in payload
			(mp.Payload[csp.MessageSize] = 0) == 0 &&

			// Same size as message
			csp.MessageSize == strlen(mp.Payload)
		){

			printf("\t\tProtocol Phase: %c\n", mp.ProtocolPhase);
			printf("\t\tProbe Sequence Number: %d\n", mp.ProbeSequenceNumber);
			printf("\t\tPayload Size: %lu\n", strlen(mp.Payload));


			// printf("Parsed MP.Payload: '%s'(%lu)\n", mp.Payload, strlen(mp.Payload));
			printf("\t\tSleeping %lf sec...\n", csp.ServerDelay);

			// Server Delay
			// sleep(csp.ServerDelay);

			nanosleep(&csp.delay, NULL);

			// Echo back
			send(clientfd, received, strlen(received), 0);

			// Increase count
			mpCount++;
		}else{

			printf("\t\tInvalid MP: %lu bytes\n%s\n", strlen(received), received);

			response = "404 ERROR: Invalid Measurement Message";

			// Respond with MP Error
			send(clientfd, response, strlen(response), 0);

			// Close connection
			mpCount = close(clientfd);

			return 0;
		}
		printf("\t--MP-->\n");
	}
	return 1;
}

int waitforCTP( int clientfd, char *received){

	// CTP
	char CTPProtocolPhase;

	// Buffer for response
	char *response;

	/* CTP */
	printf("\t<--CTP--\n");
	if(
		// Receive Message
		recv(clientfd, received, MAXBUF, 0) != -1 &&

		// Parse
		sscanf( received, "%c\n", &CTPProtocolPhase) &&

		// Protocol Phase
		CTPProtocolPhase == 't'
	){
		printf("\t\tProtocol Phase: %c\n", CTPProtocolPhase);
		response = "200 OK: Closing Connection\n";
	}else{
		printf("\t\tInvalid CTP: %s", received);
		response = "404 ERROR: Invalid Connection Termination Message";
	}

	// Respond to CTP
	send(clientfd, response, strlen(response), 0);

	// Close connection
	close(clientfd);

	printf("\t--CTP-->\n");

	return 1;
}


int main(int argc, char **argv) {

	// Variables
	int socketDescriptor,
		socketPort;

	// Enforce port argument
	if( argc != 2 || !(socketPort = atoi(argv[1])) ){
		printf("Please specify a valid port");
		exit(0);
	}

	// Setup Server
	socketDescriptor = setupServer(socketPort);

	// Bufer to receive
	char received[MAXBUF];
	
	// Handle requests
	while(1){

		printf("Waiting for client to connect...\n");

		// Accept connections from client
		int clientfd;
		struct sockaddr_in client_addr;
		socklen_t addrlen = sizeof client_addr;

		if( (clientfd = accept(socketDescriptor, (struct sockaddr*)&client_addr, &addrlen)) == -1 ){
			printf("Error accepting connection from client"); continue;
		}

		// Response to send back to client
		char *response;

		// CSP
		CSP csp;

		// Log Connected
		printf("<-- %s:%d connected\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		// Wait for CSP -- If fail, skip
		if( !waitforCSP(clientfd, received, &csp) ){ continue; }
		
		// MPs
		if( !waitforMPs(clientfd, received, csp) ){ continue; }

		// Wait for CTP
		waitforCTP(clientfd, received);

		printf("%s:%d disconnected-->\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
	}
}