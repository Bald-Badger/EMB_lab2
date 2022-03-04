/*
 *
 * CSEE 4840 Lab 2 for 2019
 *
 * Name/UNI: Shuai Zhang (sz3034)
 */
#include "fbputchar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "usbkeyboard.h"
#include <pthread.h>

/* Update SERVER_HOST to be the IP address of
 * the chat server you are connecting to
 */
/* arthur.cs.columbia.edu */
#define SERVER_HOST "128.59.19.114"
#define SERVER_PORT 42000

#define BUFFER_SIZE 128


/*
 * References:
 *
 * http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
 * http://www.thegeekstuff.com/2011/12/c-socket-programming/
 * 
 */

int sockfd; /* Socket file descriptor */

struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
void *network_thread_f(void *);

pthread_t input_thread;
void *input_thread_f(void *);

int CAPS = 0; // 1 if caps lock is on
int exit_flag = 0; // 1 then exit

unsigned char screen [ROWS][COLS];

char usb_to_ascii(uint8_t k1) {
	if (k1 == 0x00){
		return ASCII_NULL;
	}
	// if the key is among a-z
	uint8_t ascii = ASCII_NULL;
	if (KEY_A <= k1 || k1 <= KEY_Z) {
		ascii = k1 + 93;
	} 

	else if (k1 == KEY_SPACE) {
		ascii = ASCII_SPACE;
	}

	return (char) ascii;
}

void sanity_test() {
	/* Draw rows of asterisks across the top and bottom of the screen */
	for (int col = 0 ; col < 64 ; col++) {
		fbputchar('*', 0, col);
		fbputchar('*', 23, col);
	}

	fbputs("Test Text", 4, 10);
}

void print_canvas() {
	// draw a hoirizonta line
	for (int col = 0 ; col < COLS ; col++) {
		fbputchar('=', SEPREATOR_ROW, col);
	}

}


void clear_chat_space() {
	for (int row = 0; row < SEPREATOR_ROW; row ++) {
		for (int col = 0; col < COLS; col++) {
			fbputs(" ", row, col);
		}
	}
}


void clear_input_space() {
	for (int row = SEPREATOR_ROW + 1; row < ROWS; row ++) {
		for (int col = 0; col < COLS; col++) {
			fbputs(" ", row, col);
		}
	}
}


int main()
{
	int err;

	struct sockaddr_in serv_addr;

	if ((err = fbopen()) != 0) {
		fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
		exit(1);
	}

	fbclear();
	print_canvas();

	/* Open the keyboard */
	if ( (keyboard = openkeyboard(&endpoint_address)) == NULL ) {
		fprintf(stderr, "Did not find a keyboard\n");
		exit(1);
	}
		
	/* Create a TCP communications socket */
	if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
		fprintf(stderr, "Error: Could not create socket\n");
		exit(1);
	}

	/* Get the server address */
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(SERVER_PORT);
	if ( inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr) <= 0) {
		fprintf(stderr, "Error: Could not convert host IP \"%s\"\n", SERVER_HOST);
		exit(1);
	}

	/* Connect the socket to the server */
	if ( connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		fprintf(stderr, "Error: connect() failed.  Is the server running?\n");
		exit(1);
	}

	/* Start the network thread */
	pthread_create(&network_thread, NULL, network_thread_f, NULL);

	/* Start the input thread */
	pthread_create(&input_thread, NULL, input_thread_f, NULL);
	pthread_join(input_thread, NULL);

	/* Terminate the network thread */
	pthread_cancel(network_thread);

	/* Wait for the network thread to finish */
	pthread_join(network_thread, NULL);

	return 0;
}

void *network_thread_f(void *ignored)
{
	char recvBuf[BUFFER_SIZE];
	int n;
	/* Receive data */
	while ( (n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0 ) {
		recvBuf[n] = '\0';
		printf("%s", recvBuf);
		fbputs(recvBuf, 8, 0);
	}

	return NULL;
}


void *input_thread_f(void *ignored) {
	struct usb_keyboard_packet packet;
	int transferred;
	char keystate[12];
	char key;
	char message[MAX_MSG_LEN];
	char message_l1[COLS];
	char message_l2[COLS];
	int cursor = 0;
	

		/* Look for and handle keypresses */
	for (;;) {
		libusb_interrupt_transfer(keyboard, endpoint_address,
						(unsigned char *) &packet, sizeof(packet),
						&transferred, 0);

		if (transferred == sizeof(packet)) {
			sprintf(keystate, "%02x %02x %02x", packet.modifiers, packet.keycode[0],
				packet.keycode[1]);

			printf("%s\n", keystate);

			fbputs(keystate, 6, 0); // write the key hex to the frame buffer

			if (packet.keycode[0] == KEY_ESC) { /* ESC pressed? */
				break;
			}

			if (packet.keycode[0] == KEY_ENTER) {
				// should send message, clear message for now
				for (int i = 0; i < MAX_MSG_LEN; i++) {
					message[i] = ASCII_NULL;
				}
				cursor = 0;	// reset cursor
				clear_input_space();
				//scroll_one_row(USER_INPUT_L2, 1);
			}

			// change the input to ascii
			key = usb_to_ascii(packet.keycode[0]);

			if (key != ASCII_NULL) {
				if (cursor >= MAX_MSG_LEN-1) {
					printf("max message len reached!\n");
					continue;
				}
				message[cursor] = key;
				cursor ++;
				if (cursor < COLS) {
					memcpy(message_l1, &message[0], COLS * sizeof(*message));
					fbputs(message_l1, USER_INPUT_L1, 0);
				} else {
					memcpy(message_l2, &message[COLS-1], COLS * sizeof(*message));
					fbputs(message_l2, USER_INPUT_L2, 0);
				}

				if (cursor <= COLS) {
					fbputchar(ASCII_UNDERSCORE, USER_INPUT_L1, cursor);
				} else {
					fbputchar(ASCII_UNDERSCORE, USER_INPUT_L2, cursor-COLS);
				}
			} 
		}
	}
	return NULL;
}
