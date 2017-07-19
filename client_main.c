/*
*   CSC469 Winter 2016 A3
*   Instructor: Bogdan Simion
*   Date:       19/03/2016
*
*      File:      client_main.c
*      Author:    Angela Demke Brown
*      Version:   1.0.0
*      Date:      17/11/2010
*
* Please report bugs/comments to bogdan@cs.toronto.edu
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netdb.h>

#include "client.h"
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*************** GLOBAL VARIABLES ******************/

static char *option_string = "h:t:u:n:";

/* For communication with chat server */
/* These variables provide some extra clues about what you will need to
* implement.
*/
char server_host_name[MAX_HOST_NAME_LEN];

/* For control messages */
u_int16_t server_tcp_port;
struct sockaddr_in server_tcp_addr;

/* For chat messages */
u_int16_t server_udp_port;
struct sockaddr_in server_udp_addr;
int udp_socket_fd;

/* Needed for REGISTER_REQUEST */
char member_name[MAX_MEMBER_NAME_LEN];
u_int16_t client_udp_port;

/* Initialize with value returned in REGISTER_SUCC response */
u_int16_t member_id = 0;

/* For communication with receiver process */
pid_t receiver_pid;
char ctrl2rcvr_fname[MAX_FILE_NAME_LEN];
int ctrl2rcvr_qid;

/* MAX_MSG_LEN is maximum size of a message, including header+body.
* We define the maximum size of the msgdata field based on this.
*/
#define MAX_MSGDATA (MAX_MSG_LEN - sizeof(struct chat_msghdr))

char *roomname=NULL;



/******************Helper functions start*******************************************/
int snd_cntrl_msg(u_int16_t type, char *buf, int msg_len, char *reply)
{

	//initialize the socket file descript for the tcp connection
	//status code to check whether connection was established
	int tcp_socket_fd = 0;
	int status = -1;


	// reset buffer
	bzero(reply,MAX_MSG_LEN);

	//create a socket
	if ((tcp_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) <  0){
		printf("tcp socket creation failed in snd_cntrl_msg\n");
		close(tcp_socket_fd);
		return status;
	}

	//connect to the server's socket address
	if (connect(tcp_socket_fd, (struct sockaddr*)&server_tcp_addr, sizeof(server_tcp_addr)) < 0){
		printf("connection to the server failed in snd_cntrl_msg\n");
		close(tcp_socket_fd);
		return status;
	}

	//send the message
	send(tcp_socket_fd, buf, msg_len, 0);

	//read the socket into the buffer reply
	status = recv(tcp_socket_fd, reply, MAX_MSG_LEN, 0);

	close(tcp_socket_fd);
	return status;
}

int process_server_rply(char server_reply, char *room_name)
{
	//get the header portion of the server reply
	struct control_msghdr *hdr=(struct control_msghdr *)server_reply;
	//get the reply type and convert from network to host
	reply=ntohs(hdr->msg_type);
	//get the message data
	char *data=(char *)hdr->msgdata;

	//create int fot status code
	int status_code = reply;

	//if reply is any of the failures, return -1
	//else print the correct success message according to the reply code
	if (reply == REGISTER_FAIL || reply == ROOM_LIST_FAIL ||
		reply == MEMBER_LIST_FAIL || reply == SWITCH_ROOM_FAIL ||
		reply == CREATE_ROOM_FAIL){
			printf("Your request failed: %s\n", data);
			status_code = REQUEST_FAILURE;
		}
		if (reply == CREATE_ROOM_SUCC){
			printf("Created a new room: %s\n", room_name);
		}
		if (reply == REGISTER_SUCC){
			printf("Register success\n");
		}
		if (reply == MEMBER_LIST_SUCC){
			printf("%s has the following members:%s\n", room_name, data);
		}
		if (reply == SWITCH_ROOM_SUCC){
			printf("Switched your room to: %s\n", room_name);
		}
		if (reply == ROOM_LIST_SUCC){
			printf("Room list successful, here is the data:%s\n", data);
		}

		return status_code;
	}

	int room_related_request(u_int16_t type, char *room_name)
	{
		int msg_len=0;

		/* allocate a block of memory to hold the message
		* and intiliaze it to all zero */
		char *buf = (char *)malloc(MAX_MSG_LEN);
		bzero(buf, MAX_MSG_LEN);

		/* common header pointer and make it point to beginning
		* of the message*/
		struct control_msghdr *control_message_header=(struct control_msghdr *)buf;

		msg_len = sizeof(struct control_msghdr) + strlen(room_name) + 1;
		control_message_header->msg_len = htons(msg_len);
		control_message_header->member_id = htons(member_id);

		/* copy over the room name to msgdata */
		memcpy(control_message_header->msgdata, room_name, strlen(room_name) + 1);

		control_message_header->msg_type = htons(type);

		/* send the message */
		char reply[MAX_MSG_LEN];
		if (snd_cntrl_msg(REGISTER_REQUEST, buf, msg_len, reply) <= 0){
			printf("control message sending failed in room_related_request\n");
			return SERVER_FAILURE;
		}
		//get the header portion of the server reply
		struct control_msghdr *hdr=(struct control_msghdr *)reply;
		//get the reply type and convert from network to host
		reply=ntohs(hdr->msg_type);
		if(reply==SWITCH_ROOM_SUCC || reply==CREATE_ROOM_SUCC){
			roomname=room_name;
		}
		return process_server_rply(reply, room_name);
	}

	//ping the server, if it doesnt respond then that indicates failure
	int ping_periodically() {
		char result[MAX_MSG_LEN];

		//send a member keep alive signal and if it fails return SERVER_FAILURE
		if(snd_cntrl_msg(MEMBER_KEEP_ALIVE, NULL, 0, result) < 0) {
			return SERVER_FAILURE;
		}
		return 0;
	}


	//tries to reconnect the client every 5 seconds
	void reconnect_client() {
		//status to -3
		int status = -3;
		//while status is not 0
		while (status != 0){
			//try to init the client again and reconnect to the server
			printf("Attempting to reconnect to server,please wait\n");
			status = init_client();
			//if status shows server failure, sleep for 5 seconds and try again
			if(status == SERVER_FAILURE) {
				printf("Waiting for 5 seconds\n");
				sleep(5);
			} else if(status == REQUEST_FAILURE) {
				//if the request failed, that means the name is taken
				printf("Client shutting down, name is already taken,please try again\n");
				shutdown_clean();
			}
		}
		//this means that reconnect was successful, now switch to the previous room
		//the member was in
		int k=handle_switch_room_req(roomname);
		if(k==SWITCH_ROOM_FAIL){
			printf("the room you were previously in doesnt exist anymore\n");
		}
	}
	/******************Helper functions end*******************************************/



	/************* FUNCTION DEFINITIONS ***********/

	static void usage(char **argv) {

		printf("usage:\n");

		#ifdef USE_LOCN_SERVER
		printf("%s -n <client member name>\n",argv[0]);
		#else
		printf("%s -h <server host name> -t <server tcp port> -u <server udp port> -n <client member name>\n",argv[0]);
		#endif /* USE_LOCN_SERVER */

		exit(1);
	}



	void shutdown_clean() {
		/* Function to clean up after ourselves on exit, freeing any
		* used resources
		*/

		/* Add to this function to clean up any additional resources that you
		* might allocate.
		*/

		msg_t msg;

		/* 1. Send message to receiver to quit */
		msg.mtype = RECV_TYPE;
		msg.body.status = CHAT_QUIT;
		msgsnd(ctrl2rcvr_qid, &msg, sizeof(struct body_s), 0);

		/* 2. Close open fd's */
		close(udp_socket_fd);

		/* 3. Wait for receiver to exit */
		waitpid(receiver_pid, 0, 0);

		/* 4. Destroy message channel */
		unlink(ctrl2rcvr_fname);
		if (msgctl(ctrl2rcvr_qid, IPC_RMID, NULL)) {
			perror("cleanup - msgctl removal failed");
		}

		exit(0);
	}



	int initialize_client_only_channel(int *qid)
	{
		/* Create IPC message queue for communication with receiver process */

		int msg_fd;
		int msg_key;

		/* 1. Create file for message channels */

		snprintf(ctrl2rcvr_fname,MAX_FILE_NAME_LEN,"/tmp/ctrl2rcvr_channel.XXXXXX");
		msg_fd = mkstemp(ctrl2rcvr_fname);

		if (msg_fd  < 0) {
			perror("Could not create file for communication channel");
			return -1;
		}

		close(msg_fd);

		/* 2. Create message channel... if it already exists, delete it and try again */

		msg_key = ftok(ctrl2rcvr_fname, 42);

		if ( (*qid = msgget(msg_key, IPC_CREAT|IPC_EXCL|S_IREAD|S_IWRITE)) < 0) {
			if (errno == EEXIST) {
				if ( (*qid = msgget(msg_key, S_IREAD|S_IWRITE)) < 0) {
					perror("First try said queue existed. Second try can't get it");
					unlink(ctrl2rcvr_fname);
					return -1;
				}
				if (msgctl(*qid, IPC_RMID, NULL)) {
					perror("msgctl removal failed. Giving up");
					unlink(ctrl2rcvr_fname);
					return -1;
				} else {
					/* Removed... try opening again */
					if ( (*qid = msgget(msg_key, IPC_CREAT|IPC_EXCL|S_IREAD|S_IWRITE)) < 0) {
						perror("Removed queue, but create still fails. Giving up");
						unlink(ctrl2rcvr_fname);
						return -1;
					}
				}

			} else {
				perror("Could not create message queue for client control <--> receiver");
				unlink(ctrl2rcvr_fname);
				return -1;
			}

		}

		return 0;
	}



	int create_receiver()
	{
		/* Create the receiver process using fork/exec and get the port number
		* that it is receiving chat messages on.
		*/

		int retries = 20;
		int numtries = 0;

		/* 1. Set up message channel for use by control and receiver processes */

		if (initialize_client_only_channel(&ctrl2rcvr_qid) < 0) {
			return -1;
		}

		/* 2. fork/exec xterm */

		receiver_pid = fork();

		if (receiver_pid < 0) {
			fprintf(stderr,"Could not fork child for receiver\n");
			return -1;
		}

		if ( receiver_pid == 0) {
			/* this is the child. Exec receiver */
			char *argv[] = {"xterm",
			"-e",
			"./receiver",
			"-f",
			ctrl2rcvr_fname,
			0
		};

		execvp("xterm", argv);
		printf("Child: exec returned. that can't be good.\n");
		exit(1);
	}

	/* This is the parent */

	/* 3. Read message queue and find out what port client receiver is using */

	while ( numtries < retries ) {
		int result;
		msg_t msg;
		result = msgrcv(ctrl2rcvr_qid, &msg, sizeof(struct body_s), CTRL_TYPE, IPC_NOWAIT);
		if (result == -1 && errno == ENOMSG) {
			sleep(1);
			numtries++;
		} else if (result > 0) {
			if (msg.body.status == RECV_READY) {
				printf("Start of receiver successful, port %u\n",msg.body.value);
				client_udp_port = msg.body.value;
			} else {
				printf("start of receiver failed with code %u\n",msg.body.value);
				return -1;
			}
			break;
		} else {
			perror("msgrcv");
		}

	}

	if (numtries == retries) {
		/* give up.  wait for receiver to exit so we get an exit code at least */
		int exitcode;
		printf("Gave up waiting for msg.  Waiting for receiver to exit now\n");
		waitpid(receiver_pid, &exitcode, 0);
		printf("start of receiver failed, exited with code %d\n",exitcode);
	}

	return 0;
}


/*********************************************************************/

/* We define one handle_XXX_req() function for each type of
* control message request from the chat client to the chat server.
* These functions should return 0 on success, and a negative number
* on error.
*/

int handle_register_req()
{
	int msg_len=0;

	//allocate memory to hold the message
	//and intiliaze it to all zero
	char *buf = (char *)malloc(MAX_MSG_LEN);
	bzero(buf, MAX_MSG_LEN);

	//cast a header onto the memory
	struct control_msghdr *control_message_header = (struct control_msghdr *)buf;


	//cast data ptr to the start of the data in the hdr
	struct register_msgdata *data_ptr = (struct register_msgdata *)control_message_header->msgdata;

	// set message type
	control_message_header->msg_type = htons(REGISTER_REQUEST);

	// set the correct udp port of the client
	data_ptr->udp_port = htons(client_udp_port);

	// set the member name of the client
	strcpy((char *)data_ptr->member_name, member_name);

	// calculate message length
	msg_len = sizeof(struct control_msghdr) + sizeof(struct register_msgdata) + strlen(member_name) + 1;

	//convert to network short
	control_message_header->msg_len = htons(msg_len);

	// send the message
	char reply[MAX_MSG_LEN];
	if (snd_cntrl_msg(REGISTER_REQUEST, buf, msg_len, reply) <= 0){
		free(buf);
		return SERVER_FAILURE;
	}
	//process the reply
	int reply_status = process_server_rply(reply, NULL);
	//if everyting went ok, then this client is registered and has a member id
	if(reply_status == 0) {
		member_id = hdr->member_id;
	}else{
		return REQUEST_FAILURE;
	}
	//return the reply_status
	return reply_status;
}

int handle_room_list_req()
{


	int msg_len=0;

	//allocate memory to hold the message
	//and intiliaze it to all zero
	char *buf = (char *)malloc(MAX_MSG_LEN);
	bzero(buf, MAX_MSG_LEN);

	//cast a header onto the memory
	struct control_msghdr *control_message_header = (struct control_msghdr *)buf;

	// set message type
	control_message_header->msg_type = htons(ROOM_LIST_REQUEST);

	// calculate message length
	msg_len = sizeof(struct control_msghdr) + 1;

	//convert msg_len and member_id to network short
	control_message_header->msg_len = htons(msg_len);
	control_message_header->member_id = htons(member_id);

	// send the message
	char reply[MAX_MSG_LEN];
	if (snd_cntrl_msg(ROOM_LIST_REQUEST, buf, msg_len, reply) <= 0){
		free(buf);
		return SERVER_FAILURE;
	}

	//process the reply and return the status
	return process_server_rply(reply, NULL);
}

int handle_member_list_req(char *room_name)
{
	return room_related_request(MEMBER_LIST_REQUEST, room_name);
}

int handle_switch_room_req(char *room_name)
{

	return room_related_request(SWITCH_ROOM_REQUEST, room_name);
}

int handle_create_room_req(char *room_name)
{
	return room_related_request(CREATE_ROOM_REQUEST, room_name);
}


int handle_quit_req()
{


	int msg_len=0;

	//allocate memory to hold the message
	//and intiliaze it to all zero
	char *buf = (char *)malloc(MAX_MSG_LEN);
	bzero(buf, MAX_MSG_LEN);

	//cast a header onto the memory
	struct control_msghdr *control_message_header = (struct control_msghdr *)buf;

	// set message type
	control_message_header->msg_type = htons(QUIT_REQUEST);


	// calculate message length
	msg_len = sizeof(struct control_msghdr) + 1;

	//convert msg_len and member_id to network short
	control_message_header->msg_len = htons(msg_len);
	control_message_header->member_id = htons(member_id);

	//send the message,in this case ROOM_LIST_REQUEST, because like QUIT_REQUEST
	//it has no extra data in the hdr
	char reply[MAX_MSG_LEN];
	if (snd_cntrl_msg(ROOM_LIST_REQUEST, buf, msg_len, reply) <= 0){
		free(buf);
		return SERVER_FAILURE;
	}

	shutdown_clean();
	return 0;
}

//make sure you return 0 on success and REQUEST_FAILURE when the server responds but with a failure response
//and SERVER_FAILURE when snd_cntrl_msg fails
int init_client()
{
	/* Initialize client so that it is ready to start exchanging messages
	* with the chat server.
	*
	* YOUR CODE HERE
	*/

	#ifdef USE_LOCN_SERVER

	/* 0. Get server host name, port numbers from location server.
	*    See retrieve_chatserver_info() in client_util.c
	*/

	#endif

	/* 1. initialization to allow TCP-based control messages to chat server */


	/* 2. initialization to allow UDP-based chat messages to chat server */


	/* 3. spawn receiver process - see create_receiver() in this file. */


	/* 4. register with chat server */



	return 0;

}


//in this remmeber to call my ping_periodically function every 5 seconds
//to make sure server is still responsive and if ping ping_periodically returns
//SERVER_FAILURE, then call reconnect_client
void handle_chatmsg_input(char *inputdata)
{
	/* inputdata is a pointer to the message that the user typed in.
	* This function should package it into the msgdata field of a chat_msghdr
	* struct and send the chat message to the chat server.
	*/

	char *buf = (char *)malloc(MAX_MSG_LEN);

	if (buf == 0) {
		printf("Could not malloc memory for message buffer\n");
		shutdown_clean();
		exit(1);
	}

	bzero(buf, MAX_MSG_LEN);


	/**** YOUR CODE HERE ****/


	free(buf);
	return;
}


/* This should be called with the leading "!" stripped off the original
* input line.
*
* You can change this function in any way you like.
*
*/
void handle_command_input(char *line)
{
	char cmd = line[0]; /* single character identifying which command */
	int len = 0;
	int goodlen = 0;
	int result;

	line++; /* skip cmd char */

	/* 1. Simple format check */

	switch(cmd) {

		case 'r':
		case 'q':
		if (strlen(line) != 0) {
			printf("Error in command format: !%c should not be followed by anything.\n",cmd);
			return;
		}
		break;

		case 'c':
		case 'm':
		case 's':
		{
			int allowed_len = MAX_ROOM_NAME_LEN;

			if (line[0] != ' ') {
				printf("Error in command format: !%c should be followed by a space and a room name.\n",cmd);
				return;
			}
			line++; /* skip space before room name */

			len = strlen(line);
			goodlen = strcspn(line, " \t\n"); /* Any more whitespace in line? */
			if (len != goodlen) {
				printf("Error in command format: line contains extra whitespace (space, tab or carriage return)\n");
				return;
			}
			if (len > allowed_len) {
				printf("Error in command format: name must not exceed %d characters.\n",allowed_len);
				return;
			}
		}
		break;

		default:
		printf("Error: unrecognized command !%c\n",cmd);
		return;
		break;
	}

	/* 2. Passed format checks.  Handle the command */

	switch(cmd) {

		case 'r':
		result = handle_room_list_req();
		break;

		case 'c':
		result = handle_create_room_req(line);
		break;

		case 'm':
		result = handle_member_list_req(line);
		break;

		case 's':
		result = handle_switch_room_req(line);
		break;

		case 'q':
		result = handle_quit_req(); // does not return. Exits.
		break;

		default:
		printf("Error !%c is not a recognized command.\n",cmd);
		break;
	}

	/* Currently, we ignore the result of command handling.
	* You may want to change that.
	*/
	(void)result;
	return;
}

void get_user_input()
{
	char *buf = (char *)malloc(MAX_MSGDATA);
	char *result_str;

	while(TRUE) {

		bzero(buf, MAX_MSGDATA);

		printf("\n[%s]>  ",member_name);

		result_str = fgets(buf,MAX_MSGDATA,stdin);

		if (result_str == NULL) {
			printf("Error or EOF while reading user input.  Guess we're done.\n");
			break;
		}

		/* Check if control message or chat message */

		if (buf[0] == '!') {
			/* buf probably ends with newline.  If so, get rid of it. */
			int len = strlen(buf);
			if (buf[len-1] == '\n') {
				buf[len-1] = '\0';
			}
			handle_command_input(&buf[1]);

		} else {
			handle_chatmsg_input(buf);
		}
	}

	free(buf);

}


int main(int argc, char **argv)
{
	char option;

	while((option = getopt(argc, argv, option_string)) != -1) {
		switch(option) {
			case 'h':
			strncpy(server_host_name, optarg, MAX_HOST_NAME_LEN);
			break;
			case 't':
			server_tcp_port = atoi(optarg);
			break;
			case 'u':
			server_udp_port = atoi(optarg);
			break;
			case 'n':
			strncpy(member_name, optarg, MAX_MEMBER_NAME_LEN);
			break;
			default:
			printf("invalid option %c\n",option);
			usage(argv);
			break;
		}
	}

	//REMOVE ME
	printf("Header sizes:\n");
	printf("control_msghdr: %lu bytes\n",sizeof(struct control_msghdr));
	printf("chat_msghdr: %lu bytes\n",sizeof(struct chat_msghdr));
	printf("register_msgdata: %lu bytes\n",sizeof(struct register_msgdata));
	#ifdef USE_LOCN_SERVER

	printf("Using location server to retrieve chatserver information\n");

	if (strlen(member_name) == 0) {
		usage(argv);
	}

	#else

	if(server_tcp_port == 0 || server_udp_port == 0 ||
		strlen(server_host_name) == 0 || strlen(member_name) == 0) {
			usage(argv);
		}

		#endif /* USE_LOCN_SERVER */

		init_client();

		get_user_input();

		return 0;
	}
