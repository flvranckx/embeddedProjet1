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


unsigned long lastAck;
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
PROCESS(regular_node, "regular node ");
AUTOSTART_PROCESSES(&regular_node);
static struct broadcast_conn broadcast;
static struct unicast_conn uc;



static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{

  struct broadcastMessage * b = packetbuf_dataptr();
  printf("broadcast message received from %d.%d: '%d'\n",
         from->u8[0], from->u8[1], b->hop);
  if(b->reset){
    if(myhop != 2000){// not yet reseted
      myhop = 2000;
      resetNetwork();

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
  printf("node received a message from %d via %d for %d \n", u->from_addr.u8[0], from->u8[0], u->dest_addr.u8[0]);
  
  struct rootingTable * current= myRootingTable;
  while(current!=NULL){
    if(current->addr.u8[0] == from->u8[0] && current->addr.u8[1] == from->u8[1]){
      current->lastCom = clock_seconds();
      
    }
    current=current->next;
  }

  if(u->reg){
    
    printf("received reg message from %d.%d\n", from->u8[0],from->u8[1]);
  
    addToRootingTable(u->from_addr, *from);
    leds_off(LEDS_ALL);
  }
  if(linkaddr_cmp(&linkaddr_node_addr, &(u->dest_addr))){
    printf("ack received\n");
    lastAck = clock_seconds();
    return; 
  }
  
  if(linkaddr_cmp(&nullAddr, &(u->dest_addr))){
    printf("transfer message\n");
    linkaddr_t addr;
    packetbuf_copyfrom(u, sizeof(struct unicastMessage));
    addr.u8[0] = nextHop0;
    addr.u8[1] = nextHop1;
    if(!linkaddr_cmp(&addr, &linkaddr_node_addr)) {
      unicast_send(&uc, &addr);
    }
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
  registered=false;
  lastAck = clock_seconds();
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static const struct unicast_callbacks unicast_callbacks = {recv_uc, sent_uc};

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(regular_node, ev, data)
{
  
  PROCESS_EXITHANDLER(myExitHandler();)

  PROCESS_BEGIN();
  nullAddr.u8[0] = 0;
  nullAddr.u8[1] = 0;
  leds_on(LEDS_ALL);
  broadcast_open(&broadcast, 129, &broadcast_call);
  unicast_open(&uc, 146, &unicast_callbacks);
  while(1) {
    struct rootingTable * current = myRootingTable;
    if( clock_seconds() -  lastAck >60L){
      resetNetwork();
      continue;
    }
    while(current !=NULL){
      if( linkaddr_cmp(&(current->nextHop), &(current->addr)) &&  clock_seconds() - current->lastCom > 60L ){
        resetNetwork();
        current =NULL;
        continue;
      }
      current=current->next;
    }
    leds_on(LEDS_ALL);
    //printf("timer %d\n", myhop );
    // Delay 8-16 seconds 
    etimer_set(&et, CLOCK_SECOND * 10 + random_rand() % (CLOCK_SECOND * 2));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    
    if(myhop==2000){
      resetNetwork();
      etimer_set(&et, CLOCK_SECOND * 30);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    }
    else if(!registered){
     
      registered = true;
      struct unicastMessage u;
      u.data = false; 
      u.reg = true; 
      u.from_addr = linkaddr_node_addr;
      u.dest_addr = nullAddr;
      packetbuf_copyfrom(&u, sizeof(struct unicastMessage));
      linkaddr_t addr;
      addr.u8[0] = nextHop0;
      addr.u8[1] = nextHop1;
      if(!linkaddr_cmp(&addr, &linkaddr_node_addr)) {
        unicast_send(&uc, &addr);
      }
    }
    else{
      struct unicastMessage u;
      u.data = true; 
      u.reg = false;
      u.temperature = random_rand();
      u.from_addr = linkaddr_node_addr;
      u.dest_addr = nullAddr;
      packetbuf_copyfrom(&u, sizeof(struct unicastMessage));
      linkaddr_t addr;
      addr.u8[0] = nextHop0;
      addr.u8[1] = nextHop1;
      if(!linkaddr_cmp(&addr, &linkaddr_node_addr)) {
        unicast_send(&uc, &addr);
      }
    }
  }
  
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
