
/*
#include "contiki.h"
#include "random.h"
#include "net/rime/rime.h"
#include <stdbool.h> 
#include <stdlib.h>
#include <stdio.h> 

bool treeBuilded = false; 

static struct etimer et;
bool registered =false;
struct broadcastMessage{
  bool treebuilder; 
  bool reset; 
  unsigned int hop;
};
struct unicastMessage{
  linkaddr_t dest_addr;
  linkaddr_t from_addr;
  bool data;
  bool reg;
};
struct rootingTable{
  struct rootingTable * next;
  linkaddr_t addr; 
  linkaddr_t nextHop;
};
struct rootingTable * myRootingTable = NULL;
PROCESS(root, "discovery process");
AUTOSTART_PROCESSES(&root);

static struct broadcast_conn broadcast;
static struct unicast_conn uc;
void addToRootingTable(linkaddr_t addr){
  struct rootingTable * r= (struct rootingTable*)  malloc(sizeof(struct rootingTable));
  r-> addr = addr;
  struct rootingTable * current = myRootingTable; 
  if(current == NULL){
    myRootingTable = r; 
  }
  else{
    while(current->next!=NULL){
      current = current->next; 
    }
    current->next = r;
  }
    
}
void freeRoutingTable(){
  struct rootingTable* current = myRootingTable;
  if(current == NULL){
    return;
  }
  while(current->next != NULL){
    struct rootingTable *old = current;
    current = current->next;
    free(old); 
  }

}
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  struct broadcastMessage * b = packetbuf_dataptr();
  if(b->reset){
    treeBuilded = false;
  }
  printf("broadcast message received from %d.%d: '%d'\n",
         from->u8[0], from->u8[1], b->hop);
  if(b->reset){
      b->hop++;
      packetbuf_copyfrom(b, sizeof(struct broadcastMessage));
      broadcast_send(&broadcast);
      printf("reset send \n");
    }
  
}

static void
recv_uc(struct unicast_conn *c, const linkaddr_t *from)
{
  struct unicastMessage * u = packetbuf_dataptr();
  //printf("%d\n", u->from_addr.u8[0]);
  if(u->reg){
    printf("received reg message from %d.%d\n", from->u8[0],from->u8[1]);
    addToRootingTable(u->from_addr);
    
  }
}

static void 
sent_uc(struct unicast_conn *c, int status, int num_tx)
{
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(linkaddr_cmp(dest, &linkaddr_null)) {
    return;
  }
  printf("unicast message sent to %d.%d: status %d num_tx %d\n",
    dest->u8[0], dest->u8[1], status, num_tx);
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

void myExitHandler(){
  unicast_close(&uc);
  broadcast_close(&broadcast);
}
static const struct unicast_callbacks unicast_callbacks = {recv_uc, sent_uc};

PROCESS_THREAD(root, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(myExitHandler();)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);
  unicast_open(&uc, 146, &unicast_callbacks);
  while(1) {

    // Delay 2-4 seconds 
    etimer_set(&et, CLOCK_SECOND * 15+ random_rand() % (CLOCK_SECOND * 4 ));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    if(!treeBuilded){
      struct broadcastMessage b;
      b.hop = 0;
      b.treebuilder = true;
      b.reset = false;
      packetbuf_copyfrom(&b, sizeof(struct broadcastMessage));
      broadcast_send(&broadcast);
      treeBuilded = true;
      printf("broadcast message sent\n");
    }
  }
  
  PROCESS_END();
}
*/
/*---------------------------------------------------------------------------*/
/*
 * Copyright (c) 2006, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         A very simple Contiki application showing how Contiki programs look
 * \author
 *         Adam Dunkels <adam@sics.se>
 */

#include "contiki.h"
#include "random.h"
#include "net/rime/rime.h"
#include <stdbool.h> 

#include <stdio.h> /* For printf() */
#include <stdlib.h>
#include "dev/leds.h"



unsigned int myhop=2000;
unsigned int nextHop0; 
unsigned int nextHop1; 
unsigned int reset_hopLimit;

bool treeBuilded = false; 


static struct etimer et;
bool registered =false;
struct broadcastMessage{
  bool treebuilder; 
  bool reset; 
  unsigned int hop;
};
struct unicastMessage{
  linkaddr_t dest_addr;
  linkaddr_t from_addr;
  bool data;
  bool reg;
  int temperature;
  int instruction; 
};
struct rootingTable{
  struct rootingTable * next;
  linkaddr_t addr; 
  linkaddr_t nextHop;
  unsigned long lastCom; 

};
linkaddr_t nullAddr; 

struct rootingTable * myRootingTable = NULL;
/*---------------------------------------------------------------------------*/
PROCESS(root, "root node ");
AUTOSTART_PROCESSES(&root);
static struct broadcast_conn broadcast;
static struct unicast_conn uc;



static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{

  struct broadcastMessage * b = packetbuf_dataptr();
  printf("broadcast message received from %d.%d: '%d'\n",
         from->u8[0], from->u8[1], b->hop);
  if(b->reset){
    resetNetwork();
    if(myhop != 2000){// not yet reseted
      myhop = 2000;
      
      treeBuilded=false;

    }
  }
  else if(b->hop < myhop){
    myhop = b->hop;
    nextHop0 = from->u8[0];
    nextHop1 = from->u8[1];
    b->hop++;
    packetbuf_copyfrom(b, sizeof(struct broadcastMessage));
    broadcast_send(&broadcast);
  
    printf("broadcast message sent\n");
  }
}
static void
recv_uc(struct unicast_conn *c, const linkaddr_t *from)
{

  struct unicastMessage * u = packetbuf_dataptr();
  printf("root received a message from %d via %d\n", u->from_addr.u8[0], from->u8[0]);
  struct rootingTable * current= myRootingTable;
  while(current!=NULL){
    if(current->addr.u8[0] == from->u8[0] && current->addr.u8[1] == from->u8[1]){
      current->lastCom = clock_seconds();
      
    }
    current=current->next;
  }
  printf("timer updated \n");
  if(u->reg){
    
    printf("received reg message from %d.%d\n", from->u8[0],from->u8[1]);
  
    addToRootingTable(u->from_addr, *from);
    leds_off(LEDS_ALL);
  }
  if(u->dest_addr.u8[0] == from->u8[0] && u-> dest_addr.u8[1] == from->u8[1]){
    printf("ack received\n");
    return; 
  }
  printf("ack sening...\n");
  if(linkaddr_cmp(&nullAddr, &(u->dest_addr))){
    
    struct unicastMessage u2;
      u2.data = true; 
      u2.reg = false;
      u2.temperature = random_rand();
      u2.from_addr = linkaddr_node_addr;
      u2.dest_addr.u8[0] = u->from_addr.u8[0];
      u2.dest_addr.u8[1] = u->from_addr.u8[1]; 
      packetbuf_copyfrom(&u2, sizeof(struct unicastMessage));
      linkaddr_t addr;
      struct rootingTable * current = myRootingTable;
      while(current != NULL && !linkaddr_cmp(&(current->addr),&(u2.dest_addr)))
      {
        current=current->next;
      }
      if(current==NULL){
        printf("ROOTING TABLE ERROR, head:%d, trying to compare %d and %d\n", myRootingTable->addr.u8[0], u2.dest_addr.u8[0], current->addr.u8[0]);
        resetNetwork();
      }
      else{
        printf("ack send to %d\n", current->addr.u8[0]);
        unicast_send(&uc, &(current->nextHop));
        
      }
      
    //processing
  }
  else{
    struct rootingTable * current = myRootingTable;
    while(current!=NULL){
      if(linkaddr_cmp(&(current->addr), &(u->dest_addr))){
        packetbuf_copyfrom(u, sizeof(struct unicastMessage));

        unicast_send(&uc, &(current->nextHop));
      }
      current=current->next; 
    }
  }
}

static void 
sent_uc(struct unicast_conn *c, int status, int num_tx)
{
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(linkaddr_cmp(dest, &linkaddr_null)) {
    return;
  }
  printf("unicast message sent to %d.%d: status %d num_tx %d\n",
    dest->u8[0], dest->u8[1], status, num_tx);
}
void addToRootingTable(linkaddr_t addr, linkaddr_t nexthop){
  struct rootingTable * r= (struct rootingTable*)  malloc(sizeof(struct rootingTable));
  r->addr = addr;
  r->nextHop = nexthop;
  r->lastCom = clock_seconds();
  r->next=NULL;
  struct rootingTable * current = myRootingTable; 
  if(current == NULL){
    myRootingTable = r; 
  }
  else{
    while(current->next!=NULL){
      current = current->next; 
    }
    current->next = r;

  }
  
}
void freeRoutingTable(){
  struct rootingTable* current = myRootingTable;
  if(current == NULL){
    return;
  }
  while(current->next != NULL){
    struct rootingTable *old = current;
    current = current->next;
    free(old); 
  }

}
void myExitHandler(){
  unicast_close(&uc);
  broadcast_close(&broadcast);
}
void resetNetwork(){
  struct broadcastMessage b;
  b.reset = true;
  b.hop = 0;
  packetbuf_copyfrom(&b, sizeof(struct broadcastMessage));
  broadcast_send(&broadcast);
  printf("RESET ENGAGE\n");
  freeRoutingTable();
  myRootingTable=NULL;
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static const struct unicast_callbacks unicast_callbacks = {recv_uc, sent_uc};

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(root, ev, data)
{
  
  PROCESS_EXITHANDLER(myExitHandler();)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);
  unicast_open(&uc, 146, &unicast_callbacks);
  while(1) {

    // Delay 2-4 seconds 
    etimer_set(&et, CLOCK_SECOND * 15+ random_rand() % (CLOCK_SECOND * 4 ));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    if(!treeBuilded){
      struct broadcastMessage b;
      myhop=0;
      b.hop = 0;
      b.treebuilder = true;
      b.reset = false;
      packetbuf_copyfrom(&b, sizeof(struct broadcastMessage));
      broadcast_send(&broadcast);
      treeBuilded = true;
      printf("broadcast message sent\n");
    }
  }
  
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
