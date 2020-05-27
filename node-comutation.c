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
#define REAL double
#include "contiki.h"
#include "random.h"
#include "net/rime/rime.h"
#include <stdbool.h> 

#include <stdio.h> /* For printf() */
#include <stdlib.h>
#include "dev/leds.h"



#include <math.h>       


unsigned int myhop=2000;
unsigned int nextHop0; 
unsigned int nextHop1; 
unsigned int reset_hopLimit;

unsigned int node_metered=0;
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
  bool metered;
  struct data *head;
  int dataCount;
};
struct data{
  int value;
  struct data * next;
};
linkaddr_t nullAddr; 
int linreg(int n, const REAL x[], const REAL y[], REAL* m, REAL* b, REAL* r);
struct rootingTable * myRootingTable = NULL;
/*---------------------------------------------------------------------------*/
PROCESS(computation_node, "comutation node ");
AUTOSTART_PROCESSES(&computation_node);
static struct broadcast_conn broadcast;
static struct unicast_conn uc;
bool isMetered(linkaddr_t *a);




static REAL sqr(REAL x) {
    return x*x;
}

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
    if(!isMetered(from)){
      printf("transfer message\n");
      linkaddr_t addr;
      packetbuf_copyfrom(u, sizeof(struct unicastMessage));
      addr.u8[0] = nextHop0;
      addr.u8[1] = nextHop1;
      if(!linkaddr_cmp(&addr, &linkaddr_node_addr)) {
        unicast_send(&uc, &addr);
      }
      return;
    }


      struct unicastMessage u2;
      u2.data = true; 
      u2.reg = false;
      u2.temperature = u->temperature;
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
      struct data * new = malloc(sizeof(struct data));
      new->value=u->temperature;
      new->next=current->head;
      current->head = new;
      current->dataCount++;
    printf("data collected %d\n", current->dataCount);
    if(current->dataCount == 10 ){
      
      double y[10];
      double x[10];
      int i = 1;
      while(i<11){
        y[i]=current->head->value;
        x[i]=i;
        struct data *t = current->head;
        current->head = current->head->next;
        free(t);
      }
      current->dataCount=0;
      double a;
      double b;
      linreg(10,x,y,&a,&b,NULL);
      if(a>1.0){
        struct unicastMessage u3;
        u3.data = true; 
        u3.reg = false;
        u3.temperature = -1;
        u3.from_addr = linkaddr_node_addr;
        u3.dest_addr.u8[0] = u->from_addr.u8[0];
        u3.dest_addr.u8[1] = u->from_addr.u8[1]; 
        packetbuf_copyfrom(&u2, sizeof(struct unicastMessage));
        unicast_send(&uc, &(current->nextHop));
      }

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

void freeData(struct rootingTable * metered){
  struct data *d = metered->head;
  while(d!=NULL){
    struct data *temp=d->next;
    free(d);
    d = temp;
  }
}
bool isMetered(linkaddr_t *a){
  struct rootingTable * current = myRootingTable;
  while(current != NULL){
    if(linkaddr_cmp(&(current->addr),a)){
      return current->metered;
    }
  }
  return false;
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
  r->dataCount = 0;
  if(node_metered<5){
    r->metered=true;
    node_metered++;
  }
  else{
    r->metered= false;
  }
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
  node_metered = 0;
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
PROCESS_THREAD(computation_node, ev, data)
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
      u.temperature = 0;
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
int linreg(int n, const REAL x[], const REAL y[], REAL* m, REAL* b, REAL* r){
    REAL   sumx = 0.0;                      /* sum of x     */
    REAL   sumx2 = 0.0;                     /* sum of x**2  */
    REAL   sumxy = 0.0;                     /* sum of x * y */
    REAL   sumy = 0.0;                      /* sum of y     */
    REAL   sumy2 = 0.0;                     /* sum of y**2  */
    int i = 0;
    while(i<n) { 
        sumx  += x[i];       
        sumx2 += sqr(x[i]);  
        sumxy += x[i] * y[i];
        sumy  += y[i];      
        sumy2 += sqr(y[i]);
        i++; 
    } 

    REAL denom = (n * sumx2 - sqr(sumx));
    if (denom == 0) {
        // singular matrix. can't solve the problem.
        *m = 0;
        *b = 0;
        if (r) *r = 0;
            return 1;
    }

    *m = (n * sumxy  -  sumx * sumy) / denom;
    *b = (sumy * sumx2  -  sumx * sumxy) / denom;
    if (r!=NULL) {
        *r = (sumxy - sumx * sumy / n) /    /* compute correlation coeff */
              sqr((sumx2 - sqr(sumx)/n) *
              (sumy2 - sqr(sumy)/n));
    }

    return 0; 
}
/*---------------------------------------------------------------------------*/
