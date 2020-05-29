

/* This file is used to create the root node, it will be the root of the tree and also the one communicating with the python program on the host */


#include "contiki.h"
#include "random.h"
#include "net/rime/rime.h"
#include <stdbool.h> 
#include <stdio.h>
#include <stdio.h> /* For printf() */
#include <stdlib.h>
#include "dev/leds.h"



unsigned int myhop=2000; // 2000 is the default value, 2000 means the tree has not been build yet. 
unsigned int nextHop0; 
unsigned int nextHop1; 
unsigned int reset_hopLimit;

bool treeBuilded = false; 


static struct etimer et;
bool registered =false;
struct broadcastMessage{  // broadcast that are used for the building of the rooting tree
  bool treebuilder; 
  bool reset; 
  unsigned int hop;
};
struct unicastMessage{ // unicast message that are use for transmitting data, ack and valve instruction
  linkaddr_t dest_addr;
  linkaddr_t from_addr;
  bool data;
  bool reg;
  int temperature; //this is the main metrics transfered by sensor nodes. 
};
struct rootingTable{  // use to keep other nodes in memory, it's a chained list. 
  struct rootingTable * next;
  linkaddr_t addr; 
  linkaddr_t nextHop;
  unsigned long lastCom; // timeout metrics to detect broken link
  bool metered;
  struct data *head;
  int dataCount;
};
struct rootingTable* lastest;
struct data{ // use to store data sended by sensors before analysis 
  int value;
  struct data * next;
};
linkaddr_t nullAddr; 

struct rootingTable * myRootingTable = NULL; // head of the linked list of the rooting table.
/*---------------------------------------------------------------------------*/
PROCESS(root, "root node ");
AUTOSTART_PROCESSES(&root);
static struct broadcast_conn broadcast;
static struct unicast_conn uc;



static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) //broadcast are used ONLY during tree building for neighbour discovery 
{

  struct broadcastMessage * b = packetbuf_dataptr();

  if(b->reset){
    resetNetwork(); 
    if(myhop != 2000){// not yet reseted
      myhop = 2000; //default value that indicated an unbuilded tree
      treeBuilded=false;

    }
  }
  else if(b->hop < myhop){ // should not be used as the root
    myhop = b->hop;
    nextHop0 = from->u8[0];
    nextHop1 = from->u8[1];
    b->hop++;
    packetbuf_copyfrom(b, sizeof(struct broadcastMessage));
    broadcast_send(&broadcast);
  

  }
}
static void
recv_uc(struct unicast_conn *c, const linkaddr_t *from)
{

  struct unicastMessage * u = packetbuf_dataptr(); 

  struct rootingTable * current= myRootingTable;
  while(current!=NULL){
    if(current->addr.u8[0] == from->u8[0] && current->addr.u8[1] == from->u8[1]){
      current->lastCom = clock_seconds(); // timeout information
      
    }
    current=current->next;
  }

  if(u->reg){ // the reg field indicate a unicast message for the tree building
    addToRootingTable(u->from_addr, *from);
    //leds_off(LEDS_ALL); //debug
  }

  if(linkaddr_cmp(&nullAddr, &(u->dest_addr))){ // a null address indicate the message is for the root of the tree no matter its address. can be intercepted by computation nodes
    
      struct unicastMessage u2; // this is the ack message
      u2.data = true; 
      u2.reg = false;
      u2.temperature = u->temperature;
      u2.from_addr = linkaddr_node_addr;
      u2.dest_addr.u8[0] = u->from_addr.u8[0];
      u2.dest_addr.u8[1] = u->from_addr.u8[1]; 
      packetbuf_copyfrom(&u2, sizeof(struct unicastMessage));
      linkaddr_t addr;
      struct rootingTable * current = myRootingTable;
      while(current != NULL && !linkaddr_cmp(&(current->addr),&(u2.dest_addr))) // lookup in the rooting table to find the next hop 
      {
        current=current->next;
      }
      if(current==NULL){

        resetNetwork(); // in case of error, recompute the tree
      }
      else{

        unicast_send(&uc, &(current->nextHop));// send ack
        
      }
      struct data * new = malloc(sizeof(struct data)); // save the data
      new->value=u->temperature;
      new->next=current->head;
      current->head = new;
      current->dataCount++;

    if(current->dataCount == 10 ){ // if we have enough data, send it to the server trough serial
      

      int i = 1;
      while(i<11){
        char * mypointer = &(current->head->value);
	      printf("%d\n",current->head->value);
	
        struct data *t = current->head;
        current->head = current->head->next;
        free(t);
        i++;
      }
      current->dataCount=0;
      double a;
      double b;
      lastest=current;

    }
      

  }
  else{ // if the message was not for this node, transfer it. 
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
    return; // previously used for debug, legacy code, I don't dare to delete it. 
  }


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
void resetNetwork(){ // called everytime a new node is detected, a link is broken, or a node is removed. 
  struct broadcastMessage b;
  b.reset = true;
  b.hop = 0;
  packetbuf_copyfrom(&b, sizeof(struct broadcastMessage));
  broadcast_send(&broadcast);

  freeRoutingTable();
  myRootingTable=NULL;
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static const struct unicast_callbacks unicast_callbacks = {recv_uc, sent_uc};
static int serial_input_byte(unsigned char c) // serial communication receiver 
{
    if(c=='1'){ // if server sends 1, open valve 
        struct unicastMessage u3;
        u3.data = true; 
        u3.reg = false;
        u3.temperature = -1;
        u3.from_addr = linkaddr_node_addr;
        u3.dest_addr = lastest->addr;
        
        packetbuf_copyfrom(&u3, sizeof(struct unicastMessage));
        unicast_send(&uc, &(lastest->nextHop));
    }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(root, ev, data)
{
  
  PROCESS_EXITHANDLER(myExitHandler();)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);
  unicast_open(&uc, 146, &unicast_callbacks);
  while(1) {

    // Delay 2-4 seconds 
    etimer_set(&et, CLOCK_SECOND * 15+ random_rand() % (CLOCK_SECOND * 4 )); // should act every 15 sec with added randomness to avoid collisions

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    if(!treeBuilded){ // if the tree is not builded, start the neighbour discovery. 
      struct broadcastMessage b;
      myhop=0;
      b.hop = 0;
      b.treebuilder = true;
      b.reset = false;
      packetbuf_copyfrom(&b, sizeof(struct broadcastMessage));
      broadcast_send(&broadcast);
      treeBuilded = true;

    }
  }
  
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
