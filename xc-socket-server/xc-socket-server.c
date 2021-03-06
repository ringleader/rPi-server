/* 

   xc-socket-server.c	V3.0
   Copyrights - Neil Verplank 2016 (c)
   neil@capnnemosflamingcarnival.org

   Code is very loosely based on:
   multi-client-echo-server.c - a multi-client echo server 	
   Copyrights - Guy Keren 1999 (c)

   Oct 12 - updated response to incoming messages for Camera/Flash
 

   This is intended as a multi-socket select() server,
   running on a raspberry pi 3 (with wifi), in
   conjunction with the Arduino Adafruit Huzzah (the ESP2866
   wifi enabled board) to create a responsive, wirelessly
   distributed microcontroller network.  We're using it to
   link a series of flame-enabled games. 

   NOTE: We're using the RPi 3, and have configured it as
   an access point with a static IP. Except for wiringPi,
   all libraries are standard.  WiringPi makes the raspberry
   "look like" an Arduino.

       http://wiringpi.com/download-and-install/

   Also you'll need but-client.c for the button!

   To compile:

       gcc -o xc-socket-server xc-socket-server.c -lpthread -lrt -Wall


   NOTE: When DEBUG=1, the idea is that you would run this at the command
   prompt, and all debugging info is dropped into STDOUT.  When DEBUG is 0,
   the assumption is no output, and it will run as a daemon. 

*/



#include <stdlib.h>		/* Standard Library		*/
#include <stdio.h>		/* Basic I/O routines 		*/
#include <inttypes.h>
#include <time.h>		/* Time in microseconds		*/
#include <math.h>		/* Math!			*/
#include <stdlib.h>
#include <string.h>		
#include <sys/types.h>		/* standard system types 	*/
#include <sys/stat.h>
#include <netinet/in.h>		/* Internet address structures 	*/
#include <arpa/inet.h>
#include <sys/socket.h>		/* socket interface functions 	*/
#include <netdb.h>		/* host to IP resolution 	*/
#include <unistd.h>		/* for table size calculations 	*/
#include <sys/time.h>		/* for timeout values 		*/
#include <signal.h>
#include <netinet/tcp.h>	/* TCP_NODELAY 			*/





/* 
   THESE THINGS ARE WHAT YOU SHOULD SET AS A USER 

   The port this server runs on (fairly arbitrary),

   Also, maxclients is really a limit on how many "named"
   poofers we're going to index in an array.  It isn't really
   a limit on how many sockets can connect.

*/


// look into a config file! 

int	DEBUG           =0;     // 0=daemon, no output, 1=command line with output 
#define	ECHO		0	// if debugging, should we echo message? (no if Camera!)
#define	PORT		5061	// port of our carnival server   	

#define	BUFLEN		1024	// buffer length 	   	
#define maxclients      20      // max number of poofer clients	


/* 
    Rather than !@#$ing with an *actual* associative array, 
    we store the "names" of the possible poofers as strings, and
    create an enum that corresponds to the same order.  

    Be sure and add / subtract to all THREE of the following
    lists in the same way.  Or fix it.
*/

#define BUTTON		"B"		// 0
#define ENTRY   	"ENTRY"		// 1
#define SKEEBALL	"SKEEBALL"	// 2
#define LOKI      	"LOKI"		// 3
#define DRAGON		"DRAGON"	// 4
#define ORGAN		"ORGAN"		// 5
#define POPCORN		"POPCORN"	// 6
#define STRIKER		"STRIKER"	// 7
#define SIDESHOW	"SIDESHOW"	// 8
#define TICTAC		"TICTAC"	// 9
#define CAMERA 		"CAMERA"	// 10
#define FLASH		"FLASH" 	// 11
#define LULU		"LULU"		// 12

enum POOFER 
{
  button,
  entry,
  skeeball,
  loki,
  dragon,
  organ,
  popcorn,
  striker,
  sideshow,
  tictac,
  camera,
  flash,
  lulu	
} mypoofers;

/* 
These are the strings each effect (including the button) will
initially use to identify themselves.  It's important this list
correspond with the enumeration above (same order, same number)
*/

const char* game[maxclients] = {
BUTTON,
ENTRY,
SKEEBALL,
LOKI,
DRAGON,
ORGAN,
POPCORN,
STRIKER,
SIDESHOW,
TICTAC,
CAMERA,
FLASH,
LULU
};




/* 
   magical array of integers, where key is implied
   number above, value is sock fd descriptor.

   This is a way of finding the socket for a known game,
   and doesn't necessarily have all sockets in the array.
*/

int gameSocket[maxclients];


/* order in which to "poof a round" of the above" */

int roundOrder[maxclients] = {entry,loki,popcorn,sideshow,dragon,skeeball,striker,organ,lulu};

#define ROpoofLength	360 	/* milliseconds of a poof in roundOrder 	*/
#define ROpoofDelay	50 	/* milliseconds delay between poofs roundOrder 	*/



/************** OUR SUBROUTINES  ******************/


void error(char *msg);

void cur_time (void);

int naive_str2int (const char* snum);

void send_msg (int sock, fd_set wr_sock, char *msg);

void delay (unsigned int howLong);





/*  MAIN SETUP AND INFINITE LOOP */

int main(int argc,char **argv) {

    /* socket server variables */

    int			i,x;			/* index counters for loop operations 		*/
    int			rc; 			/* system calls return value storage 		*/
    int			s; 			/* socket descriptor 				*/
    int			cs; 			/* new connection's socket descriptor 		*/
    char		buf[BUFLEN+1];  	/* buffer for incoming data 			*/
    struct sockaddr_in	sa; 			/* Internet address struct 			*/
    struct sockaddr_in	csa; 			/* client's address struct 			*/
    socklen_t         	size_csa; 		/* size of client's address struct 		*/
    fd_set		rfd;     		/* set of open sockets 				*/
    fd_set		c_rfd; 			/* set of sockets available to be read 		*/
    fd_set		w_rfd; 			/* set of sockets available to be written to 	*/
    int			dsize; 			/* size of file descriptors table 		*/
    int			pushed=0; 		/* number of button currently pushed/released 	*/
    int                 butstate=0;		/* 1 if pressed, 0 if released			*/
    char                message[BUFLEN+1];	/* string for sending messages to sockets	*/
    char                message2[BUFLEN+1];	/* string for sending messages to sockets	*/
    char 	 	*array[3];		/* array for receiving messages from sockets	*/
    int 		flag = 1;		/* for TCP_NODELAY				*/
    int			result;			/* for TCP_NODELAY				*/
    int			whosTalking;		/* which socket sent a message 			*/
    int			mySock;			/* which socket to poof 			*/


/*  struct 	timeval timeout; 	 	select() timeout for testing 			*/
    
    if (argc == 2) {
        DEBUG = (int)argv[1][0]-'0';
    }

    
//    INIT SCRIPTS NOT RIGHT!!



/* 

    I note this isn't really working correctly when launched at boot.  
    In particular, stopping the daemon causes it to re-spawn, several 
    times in the case of the button.

    The problem is in the init scripts, not in this code, but.

    It does however launch on boot, which was the basic goal.
*/


    /* Make this server a DAEMON if not debugging.	*/
    if (!DEBUG) {
      pid_t process_id = 0;
      pid_t sid = 0;
    
      process_id = fork();  	// Create child process

      if (process_id < 0) {
          printf("fork failed!\n");
          exit(1);		// Return failure in exit status
      }
      
      if (process_id > 0) {	// KILL PARENT PROCESS
          // printf("process_id of child process %d \n", process_id);
          exit(0);		// return success in exit status
      }
      
      umask(0);			// unmask the file mode
      sid = setsid();		// set new session
      if(sid < 0) {
          printf("couldn't setsid\n");
          exit(1);		// Return failure
      }
      
      chdir("/");		// Change the current working directory to root.

   } else {
      printf("open for debugging!\n");fflush(stdout);
   } 

    memset(message,0,sizeof(message));

    signal(SIGPIPE, SIG_IGN);	/* ignore sigpipe */

    FD_ZERO(&rfd);		/* clear out sets */
    FD_ZERO(&c_rfd);
    FD_ZERO(&w_rfd);
    

/* Set up socket server */

    
    memset(&sa, 0, sizeof(sa)); 		/* first clear out the struct, to avoid garbage	*/
    sa.sin_family = AF_INET;			/* Using Internet address family 		*/
    sa.sin_port = htons(PORT);			/* copy port number in network byte order 	*/
    sa.sin_addr.s_addr = INADDR_ANY;		/* accept connections through any host IP 	*/
    s = socket(AF_INET, SOCK_STREAM, 0);	/* allocate a free socket 			*/

    if (s < 0) {
	error("socket: allocation failed");
    }

    /* bind the socket to the newly formed address */
    rc = bind(s, (struct sockaddr *)&sa, sizeof(sa));

    /* check there was no error */
    if (rc) {
	error("bind");
    }

    /* ask the system to listen for incoming connections	*/
    /* to the address we just bound. specify that up to		*/
    /* 5 pending connection requests will be queued by the	*/
    /* system, if we are not directly awaiting them using	*/
    /* the accept() system call, when they arrive.		*/
    rc = listen(s, 5);

    /* check there was no error */
    if (rc) {
	error("listen");
    }

    
    size_csa = sizeof(csa);		/* remember size for later usage */
    dsize = getdtablesize();		/* calculate size of file descriptors table */

    
    FD_SET(s, &rfd);	/* we initially have only one socket open */
    dsize = s;  	/* reset max socket number */


    /* enter an accept-write-close infinite loop */

    while (1) {

	/* the select() system call waits until any of	*/
	/* the file descriptors specified in the read,	*/
	/* write and exception sets given to it, is	*/
	/* ready to give data, send data, or is in an 	*/
	/* exceptional state, in respect. the call will	*/
	/* wait for a given time before returning. in	*/
	/* this case, the value is NULL, so it will	*/
	/* not timeout. 				*/


	c_rfd = rfd;
	w_rfd = rfd;
        /* null in timeout means wait until incoming data */
        rc = select(dsize+1, &c_rfd, NULL, NULL, (struct timeval *)NULL);


    /* accept incoming connections, if any, add to array */

	/* if the 's' socket is ready for reading, it	*/
	/* means that a new connection request arrived.	*/
	if (FD_ISSET(s, &c_rfd)) {
	    /* accept the incoming connection */
       	    cs = accept(s, (struct sockaddr *)&csa, &size_csa);

       	    /* check for errors. if any, ignore new connection */
       	    if (cs < 0)
       		continue;

            /* Turn off Nagle's algorithm for less delay */
            result = setsockopt(
	      cs,
              IPPROTO_TCP,    
              TCP_NODELAY,   
              (char *) &flag,
              sizeof(int));
            if (result < 0) {
              /* note, error doesn't preclude continuing */
              if (DEBUG) { printf("TCP_NODEAY failed.\n");fflush(stdout); }
              continue;
            }

	    if (DEBUG) { printf("socket received. cs:%d s:%d\n",cs,s);fflush(stdout); }

	    FD_SET(cs, &rfd);			/* add socket to set of open sockets */
            if (cs > dsize) { dsize = cs; }  	/* reset max */
	   
	    continue;
	}

    /* now see if client has anything to say, optionally echo it if they do for confirmation  */

	/* check which sockets are ready for reading.             */
        /* send changes in button status to appropriate socekt(s) */


        /* 
	   NOTE: we range from s+1 to dsize.  In fact, the first socket allocated
	   seems to be just used to bind - we dont send or receive over it.  Also,
           sockets 0, 1 and 2 correspond to STDIN, STDOUT, and STDERR, so "s" is likely
           to be 3 (presuming nothing else is running).
	  
	   So the first "real" socket is s+1, and the "last" is dsize.  Note that it's
	   possible for there to be fewer sockets than dsize-s+1, as sockets can be
	   freed up *between* s and dsize, but we just run through the range for
	   sockets available to be read.
	*/


	for (i=s+1; i<dsize+1; i++) 
	  if (FD_ISSET(i, &c_rfd)) { 
       
  	    /* 
		TECHNICALLY, I'm not sure this read() is guaranteed to read BUFLEN
		bytes, and we should confirm full receipt of message.  Given the low
		traffic and tiny message sizes, we seem to be just fine. 

		NOTE:

		There's also the chance that when a Huzzah disconnects, it's leaving
		a buffer of garbage, and we should clear it by reading to the end?
		Seems to "take a while" for a disconnected huzzah to register, and there
		also seems to be the slim chance this can cause the Pi to freeze,
		possibly because it's trying to write to a closed socket?
		Can't seem to reproduce the problem.... Although it may specifically
		happen after re-flashing a Huzzah.  Hm.  Does DEBUG mode matter?

	    */

            memset(buf,0,BUFLEN);	/* clear buffer */
	    rc = read(i, buf, BUFLEN);	/* read from the socket */
	    if (rc == 0) {

	        /* if client closed the connection, close the socket */
		close(i);
	        FD_CLR(i, &rfd);
	    	if (DEBUG) { printf("closed socket:%d\n",i);fflush(stdout); }

 		/* also find and reset corresponding associative array element to 0, if any */
	        for (x=0; x<maxclients; x++) { 
		    if (gameSocket[x] == i) { 
			gameSocket[x]=0;
	    	        if (DEBUG) { printf("gameSocket:%d (i) closed. x=%d\n",i,x);fflush(stdout); }
			x=maxclients;
		    }
                }
	    } else {
                
                /*
                  split the string on ":" 
                  first part of array is the sender, second/remaining is the message.
                  e.g. SKEEBALL:725:  or DRAGON:1:

                  the button is B:a:b:, where B means button, "a" is the number 
		  representing which button, and "b" is either 1 or 0 (has just 
 		  been pushed, or just been released).
                */

                whosTalking = i;

	        array[0] = strtok(buf,":");
                array[1] = strtok(NULL,":");

                if (array[0]) {
		    /* If this is a message from a  socket associated with a
 	            named effect, (re-)associate it.  If it's from the button,
		    get which button, and the button state.  Right now, this
		    predominantly collects the "Hi I'm this game" messages and
		    keeps track of which socket is which game.  It does not
		    track all sockets (the unnamed). */

		    for (x=0; x<maxclients; x++) {
                        if (!game[x]) continue;
		        if (strcmp(array[0],game[x])==0) { 
			    gameSocket[x] = i;
			    x = maxclients;
			}
                    }

		    /* if it's the button, get the rest of the message */ 
		    if (strcmp(array[0],game[button])==0) {
	                array[2] = strtok(NULL,":");
                        if (array[1]) {
                            pushed = naive_str2int(array[1]);
			    if (array[2]) {
                                /* 1 is pushed, 0 is released */
                                butstate = naive_str2int(array[2]);
                            }
                        }
                    }

                }

		if (DEBUG) { printf("whosTalking=socket#:%d msg:%s pushed:%d butstate:%d\n",whosTalking,buf,pushed,butstate);fflush(stdout); }

                /* 
		    echo data back to the client when debugging (send_msg filters
		    out the button, the camera, etc...)
		 */
                if (DEBUG && ECHO) {
		    send_msg(i, w_rfd, buf);
                }
	    } /* end else data to read */
	  } /* forall sockets if socket */



        /* if button pushed, write to appropriate sockets */ 
	if (pushed) {

            /* 
		Now we collect all sockets available for writing. 
	       	this should return instantly (right?).  Also helps
		clean up when Huzzahs have been uplugged (no
		longer available for writing).
  	    */

	    w_rfd = rfd;
            rc = select(dsize+1, NULL, &w_rfd, NULL, (struct timeval *)NULL);

            strcpy ( message2, "$p0%" );

            if (pushed==3 && butstate==1) {

		/* POOFSTORM */
                
                strcpy ( message, "$p2%" );

                for (i=s+1; i<dsize+1; i++) {
		    send_msg(i, w_rfd, message);
                }

            } else if (pushed==2 && butstate==1) {

		/* 
		DO A ROUND
	
                    poof the games in roundOrder if the given socket
                    is available for writing, delay between poofs
	 	*/

                if (DEBUG) { printf("Let's Have a ROUND!!\n"); cur_time(); fflush(stdout); }
                strcpy ( message, "$p1%" );

                for (i=0; i<maxclients; i++) {

                    if (roundOrder[i]) { 			/* if there's a game specified		*/
                        mySock = gameSocket[roundOrder[i]];  	/* get the socket associated		*/
                        if (FD_ISSET(mySock, &w_rfd)) { 	/* if it's available for writing	*/

                            if (DEBUG) { printf("ROUND! poofing %s(%d)\n",game[roundOrder[i]],mySock); cur_time(); fflush(stdout); }

                            if (mySock == gameSocket[lulu]) { 

				// Subroutine for special poof??
 				/* special lulu poof - we "prime the pump" */
				int xx;
				for (xx=0; xx<4; xx++) {
                                    send_msg(mySock, w_rfd, message);
                                    delay(80);
                                    send_msg(mySock, w_rfd, message2);
                                    delay(50);
				}
                                delay(500);
                                send(mySock, message, sizeof(message), MSG_NOSIGNAL);
                                delay(1600);
                                send(mySock, message2, sizeof(message2), MSG_NOSIGNAL);
			    } else { 
                                send(mySock, message, sizeof(message), MSG_NOSIGNAL);
                                delay(ROpoofLength);
                                send(mySock, message2, sizeof(message2), MSG_NOSIGNAL);

			        /* delay between poofs if game was hooked up */
                                delay(ROpoofDelay);   
			    }
                        }
                    }
                }

            } else if (pushed==1) {
                /* pushed=1 - THE button */
                if (butstate == 1) {
                    strcpy ( message, "$p1%" );
                } else {
                    strcpy ( message, "$p0%" );
                }
                for (i=s+1; i<dsize+1; i++) {
		    send_msg(i, w_rfd, message);
                    if (DEBUG) { printf("poofing=%d, i:%d\n",butstate,i); cur_time(); fflush(stdout); }
                }
            } else if (pushed>3) {
                strcpy ( message, "$p1%" );
                
                /*  Connect button #'s above 3 to a corresponding socket above the button */
                mySock = gameSocket[pushed-3];  // push next thing above the button...
		send_msg(mySock, w_rfd, message);
                if (DEBUG) { printf("poofing one poofer.  mySock:%d\n",mySock); cur_time(); fflush(stdout); }
            } 

            pushed   = 0;  /* reset each time around */
            butstate = 0;

	} else if (array[1]) {

	    /*
		we know we just got an actual message from some socket other
		than the button.

		who is it, what should we do? Answers vary.
	    */

	    if (DEBUG) { printf("Incoming message from:%s, message:%s\n",array[0],array[1]);  cur_time(); fflush(stdout); }

            if (whosTalking == gameSocket[skeeball]) {
		/* skeeball - high score - message to organ? */

            } else if (whosTalking == gameSocket[camera]) {
		/* camera - just send the message on to the flash */
		send_msg(gameSocket[flash], w_rfd, array[1]);
	        if (DEBUG) { printf("Sending poof message to flash:%s\n",array[1]);  cur_time(); fflush(stdout); }
            }
        } // end there's a message

	if (DEBUG) { printf("\n"); }

    } /* end while(1) */

    return(0);

} /* end main	*/



/*  error - wrapper for perror */
void error(char *msg) {
	perror(msg);
	exit(1);
}


/* prints the time in nanosecond precision	*/
void cur_time (void)
{
    long            ms; // Milliseconds
    time_t          s;  // Seconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    s  = spec.tv_sec;
    /* ms = spec.tv_nsec / 1.0e6; // Convert nanoseconds to milliseconds*/
    ms = spec.tv_nsec;

    printf("Current time: %lld.%ld\n", (intmax_t)s, ms);
          
}


/* 
    Turns a string into an integer by ASSUMING it's 

	- non-negative
	- contains only integer characters
	- does not exceed integer range

    Given that we're talking about buttons 0-99 max,  I feel it's ok
*/
int naive_str2int (const char* snum) {

    const int NUMLEN = (int)strlen(snum);
    int i,accum=0;
    for(i=0; i<NUMLEN; i++) {
        accum = 10*accum;
	accum += (snum[i]-0x30);
    }
    return accum;
}



/* send messages out.  confirm socket is ready for writing, and
isn't on the "no send" list (we haven't created that yet) */
void send_msg (int sock, fd_set wr_sock, char *msg) {
 
    /* 
      don't send messages to button, it isn't listening (camera either) 
      in fact, it may be that a non-receiving client could cause segfault?
    */

    if (sock != gameSocket[button] && sock != gameSocket[camera]) {
        if (FD_ISSET(sock, &wr_sock)) { 
            int nBytes = strlen(msg) + 1;
            send(sock, msg, nBytes, MSG_NOSIGNAL);
	}
    }

}


void delay (unsigned int howLong)
{
  struct timespec sleeper, dummy ;

  sleeper.tv_sec  = (time_t)(howLong / 1000) ;
  sleeper.tv_nsec = (long)(howLong % 1000) * 1000000 ;

  nanosleep (&sleeper, &dummy) ;
}








