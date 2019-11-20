////////////////////////////////////////////////////////////////////////////////
//
//  File          : hdd_client.c
//  Description   : This is the client side of the CRUD communication protocol.
//

//

// Include Files
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>

// Project Include Files
#include <hdd_network.h>
#include <cmpsc311_log.h>
#include <cmpsc311_util.h>
#include <hdd_driver.h>


// Get BlockID from HddBitCmd
int32_t getID(HddBitCmd command){
	int32_t blockID;
	command = command << 32; // shift left 32 bits to remove flags, block size, and op
	command = command >> 32; // shift right 32 bits to make command lsb
	blockID = command; 
	
	return blockID;
}

// Get the operation code from HddBitCmd
int getOpCode(uint64_t command){
	int opCode; 
	command = command >> 62;
	opCode = command; 
	return opCode; 
}

int32_t getBlockSize(uint64_t command){
	int blockSize; 
	command = command << 2; // remove op code
	command = command >> 38; // reove flags, r, and block ID
	blockSize = command; 
	return blockSize; 
}

int getFlag(HddBitCmd command){
	command = command << 28;
	command = command >> 61;
	return command; 
}

int getR(HddBitCmd command){
	int result;

	command = command << 31; // shift left 31 bits to remove flags, block size and op
	command = command >> 63; // shift right 63 to make command lsb  
	result = command; 
	
	return result; // value of either 0 on success or 1 on failure 
}

HddBitResp formatResponse(uint64_t op, uint64_t blockSize, uint64_t flags, uint64_t r, uint64_t blockID){
	op = op << 62; 
	blockSize = blockSize << 36;
	flags = flags << 33; 
	r = r << 32; 
	HddBitResp response = op | blockSize | flags | r | blockID;
	return response; 
}

int socketfd = -1; 

int initConnection(){
	//uint32_t value; 
	struct sockaddr_in caddr; 
	char *ip = HDD_DEFAULT_IP;

	caddr.sin_family = AF_INET;
	caddr.sin_port = htons(19876); // DO WE USE DEFAULT PORT HERE? 
	if ( inet_aton(ip, &caddr.sin_addr) == 0 ){
		return -1;
	}
	socketfd = socket(PF_INET, SOCK_STREAM, 0);
	// Error on socket creation 
	if (socketfd == -1){
		printf("Error on socket creation\n");
		return -1;
	}
	// Error on socket connect
	int connection = connect(socketfd, (const struct sockaddr *)&caddr, sizeof(struct sockaddr));
	if (connection == -1){
		printf("Error on socket connect\n");
		return -1;
	}

	return 0; 

}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : hdd_client_operation
// Description  : This the client operation that sends a request to the CRUD
//                server.   It will:
//
//                1) if INIT make a connection to the server
//                2) send any request to the server, returning results
//                3) if CLOSE, will close the connection
//
// Inputs       : cmd - the request opcode for the command
//                buf - the block to be read/written from (READ/WRITE)
// Outputs      : the response structure encoded as needed
HddBitResp hdd_client_operation(HddBitCmd cmd, void *buf) {
	HddBitResp fail = formatResponse(0,0,0,1,0);
	int flag = getFlag(cmd); 
	int op = getOpCode(cmd);
	int32_t size = getBlockSize(cmd); 
	// Convert value into network byte order to send
	uint64_t value = htonll64(cmd);
	HddBitResp response = 0; 

	if (flag == HDD_INIT){
		int initial = initConnection();
		printf("Initialize happened\n");
		if (initial == -1){
			return fail; 
		}
	}

	if (flag == HDD_INIT || flag == HDD_FORMAT || flag == HDD_SAVE_AND_CLOSE){
		if(op == HDD_DEVICE){
			// Send HddBitCmd and recieve HddBitResp
			printf("HDD_DEVICE OP\n");

			int w = write(socketfd, &value, sizeof(value)); // Send the data to server 
			if (w != sizeof(value)){
				int total = w;
				while(total!= sizeof(value)){
					int w = write(socketfd, &value + total, sizeof(value) - total);
					total = total + w; 
				} 
			}

			printf("Write passed\n");

			// DO WE NEED TO CHECK THAT OP TYPE MATCHES INITIAL OP TYPE? 
			int r = read(socketfd, &value, sizeof(value)); // Receive data
			if (r != sizeof(value)){
				int total = r;
				while(total!= sizeof(value)){
					int w = read(socketfd, &value + total, sizeof(value) - total);
					total = total + w; 
				} 
			}

			printf("Read passed\n");

			response = ntohll64(value); // Convert returned value to host byte order


			// Close the socket close(socketfh) and set it to -1 (on save and close request)
			if (flag == HDD_SAVE_AND_CLOSE){
				close(socketfd);
				socketfd = -1; 
				printf("SOCKET CLOSED\n");
			}

			return response; // return response from server in host byte order
		}
		return fail; 
	}

	if (flag == HDD_NULL_FLAG || flag == HDD_META_BLOCK){
		if (op == HDD_BLOCK_CREATE || op == HDD_BLOCK_OVERWRITE){
			// Send HddBitCmd and bytes of block
			// Receive HddBitResp

			printf("OP IS CREATE OR OVERWRITE\n");

			int sizeWritten = write(socketfd, &value, sizeof(value));
			if (sizeWritten != sizeof(value)){
				int total = sizeWritten;
				while(total!= sizeof(value)){
					int w = write(socketfd, &value + total, sizeof(value) - total);
					total = total + w; 
				} 
			}

			int w2 = write(socketfd, buf, size); 
			if (w2 != size){
				int total = w2; 
				while(total != size){
					int w = write(socketfd, buf + total, size- total);
					total= total + w; 
				}
			}

			printf("first 2 writes happened\n");

			int r = read(socketfd, &value, sizeof(value)); 
			if (r != sizeof(value)){
				printf("WHILE LOOOP HIT \n");
				int totalRead = r; 
				while(totalRead != sizeof(value)){
					int w = read(socketfd, &value + totalRead, sizeof(value)- totalRead);
					totalRead= totalRead + w; 
				}
			}

			printf("READS happened\n");

			response = ntohll64(value); 

			printf("RETURN HAPPENED FOR CREATE/OVERWRITE\n");
			return response; 

		}

		if (op == HDD_BLOCK_READ){
			// Send HddBitCmd
			// Receive HddBitResp and bytes of block
			int w = write(socketfd, &value, sizeof(value));
 
			if (w != sizeof(value)){
				int totalWritten = w; 
				while(totalWritten != sizeof(value)){
					int w = write(socketfd, &value + totalWritten, sizeof(value)- totalWritten);
					totalWritten= totalWritten + w; 
				} 
			}

			printf("OP IS READ, first write happened\n");

			int r = read(socketfd, &value, sizeof(value)); 
			if (r != sizeof(value)){

				int totalRead = r; 
				while(totalRead != sizeof(value)){
					int w = read(socketfd, &value + totalRead, sizeof(value)- totalRead);
					totalRead= totalRead + w; 
				}
			}

			int r2 = read(socketfd, buf, size);

			if (r2 != size){
				int totalRead = r2; 
				while(totalRead != size){
					int w = read(socketfd, buf + totalRead, size-totalRead);
	
					totalRead= totalRead + w; 
				}
			}
			
			printf("OP IS READ, first 2 reads happened\n");

			response = ntohll64(value); 

			printf("OP IS READ, response is returned \n");
			return response; 


		}

		if (op == HDD_BLOCK_DELETE){
			// Send HddBitCmd
			// Receive HddBitResp
			int w = write(socketfd, &value, sizeof(value)); // Send the data to server 
			if (w != sizeof(value)){
				int total = w;
				while(total!= sizeof(value)){
					int w = write(socketfd, &value + total, sizeof(value) - total);
					total = total + w; 
				} 
			}

			// DO WE NEED TO CHECK THAT OP TYPE MATCHES INITIAL OP TYPE? 
			int r = read(socketfd, &value, sizeof(value)); // Receive data
			if (r != sizeof(value)){
				int total = r;
				while(total!= sizeof(value)){
					int w = read(socketfd, &value + total, sizeof(value) - total);
					total = total + w; 
				}  
			}
			response = ntohll64(value); // Convert returned value to host byte order
			return response; 
		}

		return fail; // if the op is not delete, read, create or overwrite
	}


    return fail; 
}