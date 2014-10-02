#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>

#define MAXBUF 32100



typedef struct {
	double RTT;
	double TPUT;
} Measurement;



// -- Helper Functions


// Establish connection
int establishSocket( struct hostent *hostName, int hostPort ){

	// Socket
	int socketDescriptor;

	// Check if socket descriptor sucessfully referenced
	// int socket(int domain, int type, int protocol);
	if( (socketDescriptor =	socket(AF_INET, SOCK_STREAM, 0)) == -1 ){
		printf("Error creating a socket");
		exit(0);
	}

	// Server address
	struct sockaddr_in serverAddr;
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_port = htons(hostPort);
		memcpy(&serverAddr.sin_addr.s_addr, hostName->h_addr, hostName->h_length);


	// Connect to server
	if( connect(socketDescriptor, (struct sockaddr *) &serverAddr, sizeof serverAddr) != 0 ){
		printf("Error connecting to server");
		exit(0);
	}

	return socketDescriptor;
}





// Print String in Hex
void print_hex(const char *s){
	while(*s) printf("%02x", (unsigned int) *s++);
}

// Gets current time
double getWallTime(){
	struct timeval time;
	gettimeofday(&time, NULL);
	return (double)time.tv_sec + (double)time.tv_usec * .000001;
}

// Receive until a delimiter occurs
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

// Send and Receive Data - measured
double sendAndRecv( int sockfd, char *text, char *response ){

	// Time
	double rtt;

	// Send
	if( send(sockfd, text, strlen(text), 0) == -1 ){ exit(0); return 0; }

	// Save time
	rtt = getWallTime();
	// printf("\tSent %lu bytes: '%s'\n", strlen(text), text);
	// printf("\tSent in hex: '"); print_hex(text); printf("'\n");
	
	if( !recvUntil(sockfd, response, '\n') ){ exit(0); return 0; }
	rtt = getWallTime() - rtt;
	// printf("\tRecv %lu bytes: '%s'\n", strlen(response), response);
	// printf("\tRecv in hex: '"); print_hex(response); printf("'\n");

	return rtt;
}

// ----------


// ---- Assignment functions

char* CSP( char *MeasurementType, int NumberOfProbes, int MessageSize, double ServerDelay ){
	static char str[50];
	sprintf(str, "s %s %d %d %.0f\n", MeasurementType, NumberOfProbes, MessageSize, ServerDelay);
	return str;
}

char* MP( int ProbeSequenceNumber, char *Payload ){
	static char str[MAXBUF];
	sprintf(str, "m %d %s\n", ProbeSequenceNumber, Payload);
	return str;
}

char* CTP(){
	static char str[50];
	sprintf(str, "t\n");
	return str;
}


int sendCSP(int socketDescriptor, char *response, char *MeasurementType, int NumberOfProbes, int MessageSize, double ServerDelay){
	sendAndRecv( socketDescriptor, CSP(MeasurementType, NumberOfProbes, MessageSize, ServerDelay), response );

	if( strcmp(response, "200 OK: Ready\n") != 0 ){
		printf("Failed CSP: %s\n", response); exit(0);
	}
	// printf("CSP Successful\n\n");
	return 1;
}


Measurement sendMP(int socketDescriptor, char *response, int NumberOfProbes, int MessageSize, double ServerDelay){


	// Statistics
	Measurement ret;
	ret.RTT = 0;
	ret.TPUT = 0;

	int i = 1;
	while( i <= NumberOfProbes ){

		// Construct message
		char *payload = malloc(MessageSize + 1);
		memset(payload, 'a', MessageSize);
		payload[MessageSize] = 0;

		char *message = MP(i, payload);

		// printf("Sending Probe %d/%d (%d bytes) (Delay %f)\n", i, NumberOfProbes, MessageSize, ServerDelay);

		// RTT
		double rtt = sendAndRecv( socketDescriptor, message, response );

		// Total RTT
		ret.RTT += rtt;
		ret.TPUT += strlen(message)/rtt;

		// printf("RTT %fsec\n", rtt);

		// MP
		if( strcmp(response, message) == 0 ){
			// printf("Success %d\n\n", i);
			i++;
		}else{
			printf("Invalid response: %s\n", response);
			break;
		}
	}

	// Average
	ret.RTT /= NumberOfProbes;
	ret.TPUT /= NumberOfProbes;

	// printf("Average RTT: %f s\n", (double)ret.RTT/NumberOfProbes);
	// printf("Average TPUT: %f B/s\n", (double)ret.TPUT/NumberOfProbes);

	return ret;
}

int sendCTP(int socketDescriptor, char *response ){
	sendAndRecv(socketDescriptor, CTP(), response);
	if( strcmp(response, "200 OK: Closing Connection\n") != 0 ){
		printf("CTP failed"); exit(0);	
	}

	// Get Response
	// printf("%s\n", response);
	return 1;
}

Measurement execute( struct hostent *hostName, int hostPort, char *MeasurementType, int NumberOfProbes, int MessageSize, double ServerDelay ){

	// Establish connection
	int socketDescriptor = establishSocket(hostName, hostPort);

	// Response Storage
	char response[MAXBUF];

	/* CSP */
	sendCSP(socketDescriptor, response, MeasurementType, NumberOfProbes, MessageSize, ServerDelay);

	/* Send MPs */
	Measurement result = sendMP(socketDescriptor, response, NumberOfProbes, MessageSize, ServerDelay);

	/* Send CTP - Close connection */
	sendCTP(socketDescriptor, response);

	return result;
}



int main(int argc, char **argv) {

	// Host Name
	struct hostent *hostName;

	// Host Port
	int hostPort;

	if(
		// Check for Server and Port
		3 > argc ||

		// Store host
		(hostName = gethostbyname(argv[1])) == NULL ||

		// Store port
		!(hostPort = atoi(argv[2]))
	){
		printf("Please provide a valid server and port!\n");
		exit(0);
	}


	// Experiment Mode
	if( argc == 6 ){

		int
			// Number of Experiments
			Experiments = atoi(argv[3]) > 0 ? atoi(argv[3]) : 1,

			// Number of Probes		
			NumberOfProbes = atoi(argv[4]) > 0 ? atoi(argv[4]) : 1;

		// Server Delay Increase
		double ServerDelayIncrease = 0;
		sscanf(argv[5], "%lf", &ServerDelayIncrease);

		printf("Experiment Mode\n");
		printf("\tExperiments: %d\n", Experiments);
		printf("\tNumber of Probes: %d\n", NumberOfProbes);
		printf("\tServer Delay Increase: %f\n\n", ServerDelayIncrease);


		// Message Sizes
		int Payloads[] = {1, 100, 200, 400, 800, 1000, 2000, 4000, 8000, 16000, 32000},
			length = sizeof(Payloads)/sizeof(int);

		printf("Size\tRTT\tTPUT\tDelay\n");

		int i, j;


		// Without Delay
		for( i = 0; i < length; i++ ){

			// Experiment
			double expRTT = 0, expTPUT = 0;
			for( j = 0; j < Experiments; j++ ){				

				Measurement result = execute(hostName, hostPort, "rtt", NumberOfProbes, Payloads[i], 0);
				expRTT += result.RTT;
				expTPUT += result.TPUT;
			}
			printf("%d\t%f\t%f\t%f\n", Payloads[i], expRTT/Experiments, expTPUT/Experiments, 0.0);
		}

		// With Delay
		for( i = 0; i < length; i++ ){

			// Experiment
			double expRTT = 0, expTPUT = 0;
			for( j = 0; j < Experiments; j++ ){				

				Measurement result = execute(hostName, hostPort, "rtt", NumberOfProbes, Payloads[i], ServerDelayIncrease);
				expRTT += result.RTT;
				expTPUT += result.TPUT;
			}

			// Output
			printf("%d\t%f\t%f\t%f\n", Payloads[i], expRTT/Experiments, expTPUT/Experiments, ServerDelayIncrease);
		}
	}else

	// Normal Mode
	if( argc == 7 ){

		// Measurement Type
		char *MeasurementType = strcmp(argv[3], "tput") == 0 ? argv[3] : "rtt";

		int

		// Number of Probes
		NumberOfProbes = atoi(argv[4]) > 0 ? atoi(argv[4]) : 100,

		// Message Size
		MessageSize = atoi(argv[5]) > 0 ? atoi(argv[5]) : 1000;

		// Server Delay
		double ServerDelay = 0;
		sscanf(argv[6], "%lf", &ServerDelay);

		printf("Normal Mode\n");
		printf("\tMeasurement type: %s\n", MeasurementType);
		printf("\tNumber of Probes: %d\n", NumberOfProbes);
		printf("\tMessage Size (bytes): %d\n", MessageSize);
		printf("\tServer Delay (s): %f\n", ServerDelay);

		// Get Result
		Measurement result = execute(hostName, hostPort, MeasurementType, NumberOfProbes, MessageSize, ServerDelay);
		
		// Print Result
		if( strcmp(MeasurementType, "rtt") == 0 ){
			printf("RTT: %f s\n", result.RTT);
		}else{
			printf("TPUT: %f bytes/s\n", result.TPUT);
		}
	}

	// Print Usage
	else{
		printf("Usages:\n");

		printf("\tExperiment Mode\n");
		printf("\t./client <server> <port> <Number of Experiments> <Number of Probes> <Server Delay Increase (s)>\n\n");

		printf("\tNormal Mode\n");
		printf("\t./client <server> <port> <Measurement Type> <Number of Probes> <Message Size (bytes)> <Server Delay (s)>\n");

		exit(0);
	}
}