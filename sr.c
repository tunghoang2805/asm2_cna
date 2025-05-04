#include <stdlib.h>
#include <stdio.h>
#include "emulator.h"
#include "sr.h"


#define RTT  16.0       
#define WINDOWSIZE 6    
#define SEQSPACE 8 // double the window size      
#define NOTINUSE (-1)   


int ComputeChecksum(struct pkt packet)
{
  int checksum = packet.seqnum + packet.acknum;
    for(int i=0; i<20; i++)
        checksum += packet.payload[i];
    return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  return packet.checksum != ComputeChecksum(packet);
}


/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE]; 
static bool acked[WINDOWSIZE];  // Tracks ACK status
static int window_base = 0;
static int next_seq = 0;
static int window_count = 0;
           

void A_output(struct msg message) {
  if(window_count < WINDOWSIZE) {
      struct pkt packet;
      packet.seqnum = next_seq;
      packet.acknum = NOTINUSE;
      for(int i=0; i<20; i++)
          packet.payload[i] = message.data[i];
      packet.checksum = ComputeChecksum(packet);
      
      int index = next_seq % WINDOWSIZE;
      buffer[index] = packet;
      acked[index] = false;
      
      tolayer3(A, packet);
      if(window_count == 0) 
          starttimer(A, RTT);
      
      next_seq = (next_seq + 1) % SEQSPACE;
      window_count++;
  }
}


void A_input(struct pkt packet) {
  if(!IsCorrupted(packet)) {
      int ack_seq = packet.acknum;
      if(ack_seq >= window_base || ack_seq < (window_base + WINDOWSIZE) % SEQSPACE) {
          int index = ack_seq % WINDOWSIZE;
          if(!acked[index]) {
              acked[index] = true;
              // Slide window to first unACKed packet
              while(acked[window_base % WINDOWSIZE] && window_count > 0) {
                  window_base = (window_base + 1) % SEQSPACE;
                  window_count--;
              }
              if(window_count > 0)
                  starttimer(A, RTT);
              else
                  stoptimer(A);
          }
      }
  }
}

void A_timerinterrupt() {
  for(int i=0; i<WINDOWSIZE; i++) {
      if(!acked[i] && buffer[i].seqnum >= window_base && 
         buffer[i].seqnum < (window_base + WINDOWSIZE) % SEQSPACE) {
          tolayer3(A, buffer[i]);
      }
  }
  starttimer(A, RTT);
}     



void A_init(void)
{
  window_base = 0;
    next_seq = 0;
    window_count = 0;
    for(int i=0; i<WINDOWSIZE; i++)
        acked[i] = false;
}



/********* Receiver (B)  variables and procedures ************/

static struct pkt recv_window[WINDOWSIZE];
static bool received[WINDOWSIZE] = {false};
static int expected_seq = 0;  


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;

  /* if not corrupted and received packet is in order */
  if  ( (!IsCorrupted(packet))  && (packet.seqnum == expectedseqnum) ) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    packets_received++;

    /* deliver to receiving application */
    tolayer5(B, packet.payload);

    /* send an ACK for the received packet */
    sendpkt.acknum = expectedseqnum;

    /* update state variables */
    expectedseqnum = (expectedseqnum + 1) % SEQSPACE;        
  }
  else {
    /* packet is corrupted or out of order resend last ACK */
    if (TRACE > 0) 
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
    if (expectedseqnum == 0)
      sendpkt.acknum = SEQSPACE - 1;
    else
      sendpkt.acknum = expectedseqnum - 1;
  }

  /* create packet */
  sendpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % 2;
    
  /* we don't have any data to send.  fill payload with 0's */
  for ( i=0; i<20 ; i++ ) 
    sendpkt.payload[i] = '0';  

  /* computer checksum */
  sendpkt.checksum = ComputeChecksum(sendpkt); 

  /* send out packet */
  tolayer3 (B, sendpkt);
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  expectedseqnum = 0;
  B_nextseqnum = 1;
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

