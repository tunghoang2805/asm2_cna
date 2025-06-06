#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"


#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet */
#define SEQSPACE (2 * WINDOWSIZE)  /* the min sequence space for SR must be at least 2*windowsize */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ ) 
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}


/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE];  /* array for storing packets waiting for ACK */
static int windowfirst, windowlast;    /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;               /* the next sequence number to be used by the sender */
static bool acked[WINDOWSIZE];         /* array to track which packets in window have been ACKed */
static int timer_seq;                  /* sequence number of packet that has the timer */
/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on ACK */
  if ( windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for ( i=0; i<20 ; i++ ) 
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt); 

    /* put packet in window buffer */
    windowlast = (windowlast + 1) % WINDOWSIZE; 
    buffer[windowlast] = sendpkt;
    acked[windowlast] = false; /* initialize as not acknowledged */
    windowcount++;

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3 (A, sendpkt);

    /* start timer if first packet in window */
    if (windowcount == 1) {
      starttimer(A,RTT);
      timer_seq = sendpkt.seqnum;
    }
    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;  
  }
  /* if blocked,  window is full */
  else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}


/* called from layer 3, when a packet arrives for layer 4 
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
  int i, seqfirst, seqlast;


  /* if received ACK is not corrupted */ 
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
    total_ACKs_received++;

    /* check if new ACK or duplicate */
    if (windowcount != 0) {
          int seqfirst = buffer[windowfirst].seqnum;
          int seqlast = buffer[windowlast].seqnum;
          /* We need to find the exact packet this ACK corresponds to */
          for (i = 0; i < windowcount; i++) {
            int idx = (windowfirst + i) % WINDOWSIZE;
            
            if (buffer[idx].seqnum == packet.acknum) {
                /* Found the packet this ACK is for */
                if (!acked[idx]) {
                    /* This is a new ACK */
                    if (TRACE > 0)
                        printf("----A: ACK %d is not a duplicate\n", packet.acknum);
                    
                    acked[idx] = true;
                    new_ACKs++;

             /* Check if this was the packet with the timer */
             bool need_new_timer = (packet.acknum == timer_seq);
                        
             /* If the packet at windowfirst is ACKed, we can slide the window */
             while (windowcount > 0 && acked[windowfirst]) {
                 /* Reset acked status for future use of this slot */
                 acked[windowfirst] = false;
                 
                 /* Move window forward */
                 windowfirst = (windowfirst + 1) % WINDOWSIZE;
                 windowcount--;
             }
             
             /* Handle timer logic */
             if (windowcount == 0) {
                 /* No more packets, stop timer */
                 stoptimer(A);
             } else if (need_new_timer) {
                 /* If the packet with the timer was acknowledged, start a new timer */
                 /* Find first unacknowledged packet to timeout */
                 stoptimer(A);
                 
                 for (int j = 0; j < windowcount; j++) {
                     int idx = (windowfirst + j) % WINDOWSIZE;
                     if (!acked[idx]) {
                         timer_seq = buffer[idx].seqnum;
                         starttimer(A, RTT);
                         break;
                     }
                 }
             }
         } else {
             /* Duplicate ACK */
             if (TRACE > 0)
                 printf("----A: duplicate ACK received, do nothing!\n");
         }
         break;
     }
 }
}
} else {
if (TRACE > 0)
 printf("----A: corrupted ACK is received, do nothing!\n");
}
}


/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  int i;

  if (TRACE > 0)
    printf("----A: time out,resend packets!\n");

  /* only retransmit the packet that timed out */
  for (i = 0; i < windowcount; i++) {
    int idx = (windowfirst + i) % WINDOWSIZE;
    if (buffer[idx].seqnum == timer_seq && !acked[idx]) {
        if (TRACE > 0)
            printf("---A: resending packet %d\n", buffer[idx].seqnum);
        tolayer3(A, buffer[idx]);
        packets_resent++;
        starttimer(A, RTT);
        break;
    }
}
} 



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  /* initialise A's window, buffer and sequence number */
  A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
  windowfirst = 0;
  windowlast = -1;   /* windowlast is where the last packet sent is stored.  
		     new packets are placed in winlast + 1 
		     so initially this is set to -1
		   */
  windowcount = 0;
  timer_seq = 0;
    
    /* Initialize the acked array */
    for (int i = 0; i < WINDOWSIZE; i++) {
        acked[i] = false;
    }
}



/********* Receiver (B)  variables and procedures ************/

static int B_nextseqnum;                 /* the sequence number for the next packets sent by B */
static int rcv_base;                     /* base sequence number of receiver window */
static bool received[WINDOWSIZE];        /* tracks which packets have been received */
static struct pkt rcv_buffer[WINDOWSIZE]; /* buffer for out-of-order packets */

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;
  int offset;

   /* Check if packet is corrupted */
   if (!IsCorrupted(packet)) {
    /* Always print this message and count all non-corrupted packets */
    if (TRACE > 0)
        printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
    
    /* Always increment packets_received for every non-corrupted packet */
    packets_received++;
    
    /* Calculate offset in the receive window */
    offset = (packet.seqnum - rcv_base + SEQSPACE) % SEQSPACE;
    
    if (offset < WINDOWSIZE) {
        /* Packet is within our window - buffer it if new */
        if (!received[offset]) {
            rcv_buffer[offset] = packet;
            received[offset] = true;
            
            /* If we received packet at window base, deliver in-order packets */
            if (offset == 0) {
                while (received[0]) {
                    tolayer5(B, rcv_buffer[0].payload);
                    
                    /* Slide window */
                    for (i = 0; i < WINDOWSIZE - 1; i++) {
                        received[i] = received[i + 1];
                        rcv_buffer[i] = rcv_buffer[i + 1];
                    }
                    received[WINDOWSIZE - 1] = false;
                    
                    rcv_base = (rcv_base + 1) % SEQSPACE;
                }
            }
        }
    }
    
    /* Send ACK for the received packet */
    sendpkt.acknum = packet.seqnum;
/* If packet is corrupted, silently ignore it - no debug message */
    sendpkt.seqnum = B_nextseqnum;
    B_nextseqnum = (B_nextseqnum + 1) % 2;
        for (i = 0; i < 20; i++)
            sendpkt.payload[i] = '0';
    sendpkt.checksum = ComputeChecksum(sendpkt);
    tolayer3(B, sendpkt);
    }
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  rcv_base = 0;
    B_nextseqnum = 1;
    
    /* Initialize received array */
    for (int i = 0; i < WINDOWSIZE; i++) {
        received[i] = false;
    }
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)  
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}

