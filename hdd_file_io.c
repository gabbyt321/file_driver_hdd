////////////////////////////////////////////////////////////////////////////////
//
//  File           : hdd_file_io.c
//  Description    : 
//
//  Author         : Gabriella Tolotta
//  Last Modified  : 
//

// Includes
#include <malloc.h>
#include <string.h>

// Project Includes
#include <hdd_file_io.h>
#include <hdd_driver.h>
#include <cmpsc311_log.h>
#include <cmpsc311_util.h>
#include <hdd_network.h>

// Defines
#define CIO_UNIT_TEST_MAX_WRITE_SIZE 1024
#define HDD_IO_UNIT_TEST_ITERATIONS 10240


// Type for UNIT test interface
typedef enum {
	CIO_UNIT_TEST_READ   = 0,
	CIO_UNIT_TEST_WRITE  = 1,
	CIO_UNIT_TEST_APPEND = 2,
	CIO_UNIT_TEST_SEEK   = 3,
} HDD_UNIT_TEST_TYPE;

char *cio_utest_buffer = NULL;  // Unit test buffer



//
// 
//
// ----------------------- Implementation ---------------------------

// Global Structure for Files 
struct Files{
	int open; //set to 1 if open 0 if closed
	char name[128]; //file name
	int16_t fileHandle; // stores the integer file handle 
	HddBlockID blockID; // stores the block ID 
	uint32_t seekLocation; // store the current seek position 
	int exist; //1 if yes 0 if no
	int32_t blockSize; 
}file[1024]; // assume maximum number of file instances is 1024 


// ----------------------- HELPER FUNCTIONS ----------------------- 

// Setup command block for use in hdd_client_operation to CREATE
HddBitCmd set_block_create(int32_t blockID, int32_t count){
	HddBitCmd command = 0; 
	uint64_t setup1 = count; //represents block size space
	uint64_t setup2 = blockID; //represents block ID space
	
	setup1 = setup1 << 36; // blockID space, r space, and flag space are set to 0
	
	command = setup1 | setup2; // command equals the first 32 bit setup | second 32 bit setup
	
	return command;
}

// Setup command block for use in hdd_client_operation to READ
HddBitCmd set_block_read(int32_t blockID, int32_t blockSize){
	HddBitCmd command = 0;
	uint64_t setup1 = 1; // represents space for operation (read operation = 1)
	uint64_t setup2 = blockSize; // represents block size space
	uint64_t setup3 = blockID; // represents blockID space

	setup1 = setup1 << 62; // first 2 bits of 64 bit are represented as 2 bit operation command 
	setup2 = setup2 << 36; // next 26 bits are represented as block size 
	
	command = setup1 | setup2 | setup3; // command equals the the combination of all 3 setups

	return command; 

}

// Setup command block for use in hdd_client_operation to OVERWRITE
HddBitCmd set_block_overwrite(int32_t blockID, int32_t blockSize){
	HddBitCmd command = 0;
	uint64_t setup1 = 2; // represents space for operation (overwrite operation = 2)
	uint64_t setup2 = blockSize; // represents block size space
	uint64_t setup3 = blockID; // represents blockID space

	setup1 = setup1 << 62; // first 2 bits of 64 bit are represented as 2 bit operation command 
	setup2 = setup2 << 36; // next 26 bits are represented as block size 
	
	command = setup1 | setup2 | setup3; // command equals the the combination of all 3 setups

	return command; 

}

// Setup command to delete the hdd_content.svd file and clear the block storage of all blocks
HddBitCmd set_command_format(){
	HddBitCmd command = 0;
	uint64_t op = HDD_DEVICE;
	uint64_t flag = HDD_FORMAT;
	op = op << 62;
	flag = flag << 33; 	
	command = op | flag;

	return command; 
}

// Setup command to create the hdd_content.svd file and unitialize the device 
HddBitCmd set_command_save_and_close(){
	HddBitCmd command = 0;
	uint64_t op = HDD_DEVICE;
	uint64_t flag = HDD_SAVE_AND_CLOSE;
	op = op << 62;
	flag = flag << 33; 	
	command = op | flag;

	return command; 
}

// Setup command to perform operation to metablock
HddBitCmd set_metablock_command(uint64_t op, uint64_t blockSize){
	HddBitCmd command = 0;
	op = op << 62;
	blockSize = blockSize << 36; 
	uint64_t flag = HDD_META_BLOCK;
	flag = flag << 33;

	command = op | blockSize | flag;
	return command; 

}

// setup command to perform hdd_initialize using the hdd_client_operation function 
HddBitCmd set_hdd_initialize_command(){
	HddBitCmd command = 0; 
	uint64_t op = HDD_DEVICE; 
	uint64_t flag = HDD_INIT;
	op = op << 62;
	flag = flag << 33;
	command = op | flag; 
	return command; 
}

HddBitCmd set_delete_block_command(uint64_t blockID){
	HddBitCmd command = 0; 
	uint64_t op = HDD_BLOCK_DELETE;
	op = op << 62;
	command = op | blockID;
	return command; 
}
// Get result from HddBitResp
int32_t getResult(HddBitResp response){
	int32_t result;

	response = response << 31; // shift left 31 bits to remove flags, block size and op
	response = response >> 63; // shift right 63 to make response lsb  
	result = response; 
	
	return result; // value of either 0 on success or 1 on failure 
}

// Get BlockID from HddBitResp
int32_t getBlockID(HddBitResp response){
	int32_t blockID;
	response = response << 32; // shift left 32 bits to remove flags, block size, and op
	response = response >> 32; // shift right 32 bits to make response lsb
	blockID = response; 
	
	return blockID;
}

int initialize = 0; // 0 if block has not been initialized 
int metablockSize = 0; 


////////////////////////////////////////////////////////////////////////////////
//
// Function     : hdd_format
// Description  : ????
//
// Inputs       : ????
// Outputs      : ????
//
uint16_t hdd_format(void) {
	if ( initialize == 0 ){ // if the block has not yet been initialized 
		HddBitCmd initc = set_hdd_initialize_command();
		HddBitResp initr = hdd_client_operation(initc, NULL);
		int res = getResult(initr);

		if (res == 1){ // if hdd_client_operation failed, return failure
			return -1;
		}
		else{
			initialize = 1; 
		}
	}

	 // if the block has been initialized 
		HddBitCmd command = set_command_format(); // setup command to format device
		HddBitResp response = hdd_client_operation(command, NULL);
		int result = getResult(response); 
		if (result == 1){
			return -1; // hdd data lane failed 
		}
		else {

			// create the meta block and default global structure to it 
			int l; 
			for (l = 0; l < 1024; l++){
				file[l].fileHandle = 0;
				file[l].open = 0;
				file[l].exist = 0;
				file[l].name[0] = '\0'; 
				file[l].seekLocation = 0;
				file[l].blockSize = 0; //new for assign4
			}

			int *data = file;
			uint32_t blockSize = sizeof(file); 
			
			HddBitCmd command2 = set_metablock_command(HDD_BLOCK_CREATE, blockSize);
			HddBitResp response2 = hdd_client_operation(command2, data);
			int result2 = getResult(response2);

			if (result2 == 1){
				return -1;
			}
			else{ // successfully created metablock 
				metablockSize = blockSize; // update gloabl variable with metablock size
				return 0; 
			}
		}
		
	return -1;
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : hdd_mount 
// Description  : ????
//
// Inputs       : ????
// Outputs      : ????
//
uint16_t hdd_mount(void) {
	if ( initialize == 0 ){ // if the block has not yet been initialized 
		HddBitCmd initc = set_hdd_initialize_command();
		HddBitResp initr = hdd_client_operation(initc, NULL);
		int res = getResult(initr);

		if (res == 1){ // if hdd_client_operation failed, return failure
			return -1;
		}
		else{
			initialize = 1; 
		}
	}

	// device has been initialized 



	// Read from the metablock to populate struct with previously saved values 
		
		int blockSize = sizeof(file); // new for assign4
		//int blockSize = hdd_read_block_size(0, HDD_META_BLOCK); // get size of metablock 
		int *data = (int*) malloc(blockSize); // size of data buffer is blockSize for metablock 

		// use hdd data lane to fill data buffer with metablock data 
		HddBitCmd command = set_metablock_command(HDD_BLOCK_READ, blockSize);
		HddBitResp response = hdd_client_operation(command, data);
		int result = getResult(response);
		if(result == 1){
			free(data); // free memory 
			return -1; // failure
		}
		else{
			memcpy(file, data, blockSize); // copy memory from data read to global structure
			free(data); 
			return 0; 
		}
			
	return -1;
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : hdd_unmount
// Description  : ????
//
// Inputs       : ????
// Outputs      : ????
//
uint16_t hdd_unmount(void) {
	int *data = file; // data buffer points to global array 

	uint32_t blockSize = sizeof(file); 

	HddBitCmd command = set_metablock_command(HDD_BLOCK_OVERWRITE, blockSize); // save current state of struct
	HddBitResp response = hdd_client_operation(command, data);
	int result = getResult(response);


	if (result == -1){
		return -1; // failure from hdd data lane
	}
	else{
		metablockSize = blockSize; 
		// send save and close request 
		HddBitCmd command2 = set_command_save_and_close();
		HddBitResp response2 = hdd_client_operation(command2, NULL);
		int result2 = getResult(response2);
		if (result2 == -1){
			return -1; // failure from hdd data lane
		}
		else{
			// create the meta block and default global structure to it 
			int k; 
			for (k = 0; k < 1024; k++){
				file[k].fileHandle = 0;
				file[k].open = 0;
				file[k].exist = 0;
				file[k].name[0] = '\0'; 
				file[k].seekLocation = 0;
				file[k].blockSize = 0; 
			}


			return 0; // successfully sent save and close request
		}
	}

	return -1; 
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : hdd_open
// Description  : ????
//
// Inputs       : ????
// Outputs      : ????
//
int16_t hdd_open(char *path) {

	// loop through files in structure 
	int i; 
	for( i = 0; i <1024; i++){
		int x = strcmp((char*)file[i].name, path);
		if(x == 0){ // if there is already a designated file handle for that path 
			file[i].open = 1;
			return i;
		}
	}

	int j; 
	for(j = 0; j<1024; j++){
		int y= strcmp("\0", (char*)file[j].name);
		if(y == 0){
			file[j].open = 1; // initialize open to 1 (1 = open, 0 = closed)
			//int size = sizeof(path); 
			strcpy(file[j].name, path); // copy path to file name variable 
			file[j].blockID = 0;
			file[j].fileHandle = j;
			file[j].seekLocation = 0;
			file[j].exist = 1; 
			file[j].blockSize = 0; // initialize blockSize to zero
			return j; 
		}

	}
	return -1;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : hdd_close
// Description  : ????
//
// Inputs       : ????
// Outputs      : ????
//
int16_t hdd_close(int16_t fh) {
	if (file[fh].open == 1){  
		file[fh].open = 0; // set file to closed (open = 1, closed = 0)
		file[fh].seekLocation = 0; 
		return 0;
	}
	
	else{ //the file was already closed 
		return -1;
	}


}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : hdd_read
// Description  : ????
//
// Inputs       : ????
// Outputs      : ????
//
int32_t hdd_read(int16_t fh, void * data, int32_t count) {

	int32_t blockSize = file[fh].blockSize; //new for assign 4
	//blockSize = hdd_read_block_size(file[fh].blockID, HDD_NULL_FLAG); // get blockSize for fh 

	//Create pointer to populate with current data in the block 
	char *oldData;  
	oldData =  (char*) malloc(blockSize); //allocate size of oldData to be blockSize
	HddBitCmd command = set_block_read(file[fh].blockID, blockSize); 
	HddBitResp response = hdd_client_operation(command, oldData); // store data from read in the oldData buffer
			
	int result = getResult(response);
	if (result == 1 || file[fh].blockID == 0 || file[fh].open == 0){ //if hdd_client_operation failed, or block does not exist or file is closed 
		return -1; // failure 
	}	
	
		
	else{	
		int condition = count + file[fh].seekLocation; 

		// if count + seek position is greater than block size, read bytes from seek to blocksize 
		if (blockSize < condition){
			int copySize = blockSize - file[fh].seekLocation; // amount of data that is read
			memcpy(data, oldData + file[fh].seekLocation, copySize); // copy current data read to data buffer
			
			free(oldData); // free mem in oldData pointer to prevent mem leak 

			// update global data structure 
			file[fh].seekLocation = blockSize; 
				 
			return copySize;
		}

		//else, read bytes from seek to seek+count 
		else{ 
			memcpy(data, oldData + file[fh].seekLocation, count); // copy current data read to data buffer
			
			free(oldData); // free mem in oldData pointer to prevent mem leak 

			// update global data structure 
			file[fh].seekLocation = file[fh].seekLocation + count;

			return count; 
		}
	}
	return -1;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : hdd_write
// Description  : ????
//
// Inputs       : ????
// Outputs      : ????
//
int32_t hdd_write(int16_t fh, void *data, int32_t count) {
	if (file[fh].seekLocation + count > HDD_MAX_BLOCK_SIZE || file[fh].open == 0){ // if the size to write exceeds Max or file is closed
		return -1; // return failure 
	}

	// if block ID in global structure equals zero
	// the block has not yet been created and seek position = 0 
	if (file[fh].blockID == 0){
		HddBitCmd command = set_block_create(file[fh].blockID, count); // use helper function 
		HddBitResp response = hdd_client_operation(command, data); 
		int result = getResult(response); // use helper function 
		if (result == 1 ){ // failure response from hdd_client_operation
			return -1;
		}
		else{ // hdd_client_operation passed 
			file[fh].blockID = getBlockID(response); // store block ID in global struct
			file[fh].seekLocation = count; // the seek position of the file becomes the count 
			file[fh].blockSize = count; // new for assign 4

			return count;
		}
		
	}

	//if blockID exists 
	else{
		int32_t blockSize = file[fh].blockSize; // new for assign 4
		
		int condition = file[fh].seekLocation + count; // total bytes of data to be written

		if ( blockSize < condition){ 
		// the block size is less than the the size of data

			char *oldData; //pointer for old data in block 
			oldData =  (char*) malloc(blockSize); //allocate size of oldData to be blockSize

			HddBitCmd command = set_block_read(file[fh].blockID, blockSize);
			HddBitResp response = hdd_client_operation(command, oldData); // point to memory location of oldData 
			
			int result = getResult(response);
			if (result == 1){
				return -1; // failure response from hdd_client_operation 
			}	
			
			else{
				// create data buffer with old data at seek point and appended new 
				// data (from input buffer)
				char *newData;
				newData = (char*) malloc(condition ); 
				memcpy(newData, oldData, file[fh].seekLocation); // append old data to seek
				memcpy(newData + file[fh].seekLocation , data, count); // append new data 
				free(oldData); // free memory used no longer to prevent memory leak 
			
				// delete old block
				HddBitCmd delcommand = set_delete_block_command(file[fh].blockID); 
				HddBitCmd delresponse = hdd_client_operation(delcommand, NULL);
				int del = getResult(delresponse);

				if (del == 1){
					return -1; // failure response from deleting block using hdd client operation 
				} 
				
				else{
					HddBitCmd command = set_block_create(0, condition); // set blockID to zero 
					HddBitResp response = hdd_client_operation(command, newData); 
					int result = getResult(response);

					if (result == 1){ // failure response from hdd_client_operation
						return -1;
					}

					else{ // hdd_client_operation passed 
						free(newData); // free mem no longer used to prevent memory leak 
						file[fh].blockID = getBlockID(response); // store block ID 		
						file[fh].blockSize = condition; // assign4 update global block size
						file[fh].seekLocation = condition; // new block size

						return count; 
					}
				};
			}
		}
		
		else{ // the block size can fit the the data to be appended 
			char *oldData; //pointer for old data in block 
			oldData =  (char*) malloc(blockSize); //allocate size of oldData to be blockSize

			HddBitCmd command = set_block_read(file[fh].blockID, blockSize);
			HddBitResp response = hdd_client_operation(command, oldData); // point to memory location of oldData 
			
			int result = getResult(response);
			if (result == 1){
				free(oldData);
				return -1; // failure response from hdd_client_operation 
			}	
			
			else{
				if(condition == blockSize){ // when the block size = seek position + count 
				
					char *newData;
					newData = (char*) malloc(blockSize);
					memcpy(newData, oldData, file[fh].seekLocation); // write old data up to seek location 
					memcpy(newData+file[fh].seekLocation, data, count); // write count data up to blockSize
					
					free(oldData); // free mem no longer used to prevent memory leak 

					// overwrite block with new data
					HddBitCmd command = set_block_overwrite(file[fh].blockID, blockSize);
					HddBitResp response = hdd_client_operation(command, newData);
					int result = getResult(response);

					if (result == 1){
						free(newData);
						return -1; // failure from hdd_client_operation
					}
					
					else {
						free(newData); // free mem no longer used;
						file[fh].seekLocation = condition;

						return count; 
					}	
				}
				
				else{ //block size > seek position + count
					char *newData;
					newData = (char*) malloc(blockSize);
					memcpy(newData, oldData, file[fh].seekLocation); // write old data up to seek location
					memcpy(newData + file[fh].seekLocation, data, count); // write count data up to seekLocation + count

					int leftoverPos = blockSize - file[fh].seekLocation - count; // amount of old data after the overwritten
					memcpy(newData + file[fh].seekLocation + count, oldData + file[fh].seekLocation + count, leftoverPos);
					
					free (oldData); // free memory 

					// overwrite block with new data
					HddBitCmd command = set_block_overwrite(file[fh].blockID, blockSize);
					HddBitResp response = hdd_client_operation(command, newData);
					int result = getResult(response);

					if (result == 1){
						free(newData);
						return -1;
					}
					
					else {
						free(newData); // free mem no longer used
						file[fh].seekLocation = condition;

						return count; 
					}						
				}

			}

		} 
	}
	return -1;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : hdd_seek
// Description  : ????
//
// Inputs       : ????
// Outputs      : ????
//
int32_t hdd_seek(int16_t fh, uint32_t loc) {
	int32_t blockSize;
	blockSize = file[fh].blockSize; // get blockSize for fh 

	if(blockSize < loc || loc < 0 || file[fh].open == 0){ // if the seeking is out of range with the file 
		return -1;
	}
	
	else{
		file[fh].seekLocation = loc; 
		
		return 0;
	}
	
	return -1;
}




////////////////////////////////////////////////////////////////////////////////
//
// Function     : hddIOUnitTest
// Description  : Perform a test of the HDD IO implementation
//
// Inputs       : None
// Outputs      : 0 if successful or -1 if failure

int hddIOUnitTest(void) {

	// Local variables
	uint8_t ch;
	int16_t fh, i;
	int32_t cio_utest_length, cio_utest_position, count, bytes, expected;
	char *cio_utest_buffer, *tbuf;
	HDD_UNIT_TEST_TYPE cmd;
	char lstr[1024];

	// Setup some operating buffers, zero out the mirrored file contents
	cio_utest_buffer = malloc(HDD_MAX_BLOCK_SIZE);
	tbuf = malloc(HDD_MAX_BLOCK_SIZE);
	memset(cio_utest_buffer, 0x0, HDD_MAX_BLOCK_SIZE);
	cio_utest_length = 0;
	cio_utest_position = 0;

	// Format and mount the file system
	if (hdd_format() || hdd_mount()) {
		logMessage(LOG_ERROR_LEVEL, "HDD_IO_UNIT_TEST : Failure on format or mount operation.");
		return(-1);
	}

	// Start by opening a file
	fh = hdd_open("temp_file.txt");
	if (fh == -1) {
		logMessage(LOG_ERROR_LEVEL, "HDD_IO_UNIT_TEST : Failure open operation.");
		return(-1);
	}

	// Now do a bunch of operations
	for (i=0; i<HDD_IO_UNIT_TEST_ITERATIONS; i++) {

		// Pick a random command
		if (cio_utest_length == 0) {
			cmd = CIO_UNIT_TEST_WRITE;
		} else {
			cmd = getRandomValue(CIO_UNIT_TEST_READ, CIO_UNIT_TEST_SEEK);
		}
		logMessage(LOG_INFO_LEVEL, "----------");

		// Execute the command
		switch (cmd) {

		case CIO_UNIT_TEST_READ: // read a random set of data
			count = getRandomValue(0, cio_utest_length);
			logMessage(LOG_INFO_LEVEL, "HDD_IO_UNIT_TEST : read %d at position %d", count, cio_utest_position);
			bytes = hdd_read(fh, tbuf, count);
			if (bytes == -1) {
				logMessage(LOG_ERROR_LEVEL, "HDD_IO_UNIT_TEST : Read failure.");
				return(-1);
			}

			// Compare to what we expected
			if (cio_utest_position+count > cio_utest_length) {
				expected = cio_utest_length-cio_utest_position;
			} else {
				expected = count;
			}
			if (bytes != expected) {
				logMessage(LOG_ERROR_LEVEL, "HDD_IO_UNIT_TEST : short/long read of [%d!=%d]", bytes, expected);
				return(-1);
			}
			if ( (bytes > 0) && (memcmp(&cio_utest_buffer[cio_utest_position], tbuf, bytes)) ) {

				bufToString((unsigned char *)tbuf, bytes, (unsigned char *)lstr, 1024 );
				logMessage(LOG_INFO_LEVEL, "CIO_UTEST R: %s", lstr);
				bufToString((unsigned char *)&cio_utest_buffer[cio_utest_position], bytes, (unsigned char *)lstr, 1024 );
				logMessage(LOG_INFO_LEVEL, "CIO_UTEST U: %s", lstr);

				logMessage(LOG_ERROR_LEVEL, "HDD_IO_UNIT_TEST : read data mismatch (%d)", bytes);
				return(-1);
			}
			logMessage(LOG_INFO_LEVEL, "HDD_IO_UNIT_TEST : read %d match", bytes);


			// update the position pointer
			cio_utest_position += bytes;
			break;

		case CIO_UNIT_TEST_APPEND: // Append data onto the end of the file
			// Create random block, check to make sure that the write is not too large
			ch = getRandomValue(0, 0xff);
			count =  getRandomValue(1, CIO_UNIT_TEST_MAX_WRITE_SIZE);
			if (cio_utest_length+count >= HDD_MAX_BLOCK_SIZE) {

				// Log, seek to end of file, create random value
				logMessage(LOG_INFO_LEVEL, "HDD_IO_UNIT_TEST : append of %d bytes [%x]", count, ch);
				logMessage(LOG_INFO_LEVEL, "HDD_IO_UNIT_TEST : seek to position %d", cio_utest_length);
				if (hdd_seek(fh, cio_utest_length)) {
					logMessage(LOG_ERROR_LEVEL, "HDD_IO_UNIT_TEST : seek failed [%d].", cio_utest_length);
					return(-1);
				}
				cio_utest_position = cio_utest_length;
				memset(&cio_utest_buffer[cio_utest_position], ch, count);

				// Now write
				bytes = hdd_write(fh, &cio_utest_buffer[cio_utest_position], count);
				if (bytes != count) {
					logMessage(LOG_ERROR_LEVEL, "HDD_IO_UNIT_TEST : append failed [%d].", count);
					return(-1);
				}
				cio_utest_length = cio_utest_position += bytes;
			}
			break;

		case CIO_UNIT_TEST_WRITE: // Write random block to the file
			ch = getRandomValue(0, 0xff);
			count =  getRandomValue(1, CIO_UNIT_TEST_MAX_WRITE_SIZE);
			// Check to make sure that the write is not too large
			if (cio_utest_length+count < HDD_MAX_BLOCK_SIZE) {
				// Log the write, perform it
				logMessage(LOG_INFO_LEVEL, "HDD_IO_UNIT_TEST : write of %d bytes [%x]", count, ch);
				memset(&cio_utest_buffer[cio_utest_position], ch, count);
				bytes = hdd_write(fh, &cio_utest_buffer[cio_utest_position], count);
				if (bytes!=count) {
					logMessage(LOG_ERROR_LEVEL, "HDD_IO_UNIT_TEST : write failed [%d].", count);
					return(-1);
				}
				cio_utest_position += bytes;
				if (cio_utest_position > cio_utest_length) {
					cio_utest_length = cio_utest_position;
				}
			}
			break;

		case CIO_UNIT_TEST_SEEK:
			count = getRandomValue(0, cio_utest_length);
			logMessage(LOG_INFO_LEVEL, "HDD_IO_UNIT_TEST : seek to position %d", count);
			if (hdd_seek(fh, count)) {
				logMessage(LOG_ERROR_LEVEL, "HDD_IO_UNIT_TEST : seek failed [%d].", count);
				return(-1);
			}
			cio_utest_position = count;
			break;

		default: // This should never happen
			CMPSC_ASSERT0(0, "HDD_IO_UNIT_TEST : illegal test command.");
			break;

		}

	}

	// Close the files and cleanup buffers, assert on failure
	if (hdd_close(fh)) {
		logMessage(LOG_ERROR_LEVEL, "HDD_IO_UNIT_TEST : Failure close close.", fh);
		return(-1);
	}
	free(cio_utest_buffer);
	free(tbuf);

	// Format and mount the file system
	if (hdd_unmount()) {
		logMessage(LOG_ERROR_LEVEL, "HDD_IO_UNIT_TEST : Failure on unmount operation.");
		return(-1);
	}

	// Return successfully
	return(0);
}
