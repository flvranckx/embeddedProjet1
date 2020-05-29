

#include "contiki.h"
#include "random.h"
#include "net/rime/rime.h"
#include <stdbool.h> 

#include <stdio.h> /* For printf() */
#include <stdlib.h>
#include "dev/leds.h"



unsigned int myhop=2000; // default value of 2000 indicate that the tree is not build, otherwise store distance to root
unsigned int nextHop0; 
unsigned int nextHop1; 
unsigned int reset_hopLimit; //hop limit for the reset flooding
unsigned int ledTimer;
bool ledOn=false;

unsigned long lastAck; //timeout detection 
static struct etimer et;
bool registered =false;
struct broadcastMessage{ // used to reset the tree or for neightbour discovery 
  bool treebuilder; 
  bool reset; 
  unsigned int hop;
};
struct unicastMessage{ // main structure for message between nodes
  linkaddr_t dest_addr;
  linkaddr_t from_addr;
  bool data;
  bool reg;
  int temperature;
};
struct rootingTable{ // a simpler version than for the root and computation nodes as these node don't need to collect data but have to know their children
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
  

  if(b->reset){ // reset instruction, triggers flooding and recomputation
    if(myhop != 2000){
      myhop = 2000;
      resetNetwork();

    }
  }
  else if(b->hop < myhop){ // neightbour discovery
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
      current->lastCom = clock_seconds(); // timeout detection
      
    }
    current=current->next;
  }

  if(u->reg){ //registration of a child node
    
    
  
    addToRootingTable(u->from_addr, *from);

  }
  if(linkaddr_cmp(&linkaddr_node_addr, &(u->dest_addr))){ // check if this is the destination of the received message
    if(u->temperature==-1){
      ledOn = true;
      leds_on(LEDS_ALL);
      ledTimer=clock_seconds;
    }
    else
     
    lastAck = clock_seconds();
    return; 
  }
  
  if(linkaddr_cmp(&nullAddr, &(u->dest_addr))){ // a null addresse means the message are for the root, transfer to parent 
   
    linkaddr_t addr;
    packetbuf_copyfrom(u, sizeof(struct unicastMessage));
    addr.u8[0] = nextHop0;
    addr.u8[1] = nextHop1;
    if(!linkaddr_cmp(&addr, &linkaddr_node_addr)) {
      unicast_send(&uc, &addr);
    }
  }
  else{ // the message is for a child, transfer to child.
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
  if(linkaddr_cmp(dest, &linkaddr_null)) { // debug legacy code, I don't dare to delete 
    return;
  }
  
  
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
void resetNetwork(){ // triggers a recomputation of the network by flooding
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

  broadcast_open(&broadcast, 129, &broadcast_call);
  unicast_open(&uc, 146, &unicast_callbacks);
  while(1) {
    struct rootingTable * current = myRootingTable;
    if( clock_seconds() -  lastAck >60L){ // timeout of parent
      resetNetwork(); // trigger recomputation of the network
      continue;
    }
    if(ledOn && (clock_seconds() -ledTimer > 10L )){ // close de valve, it has been open for 10 sec 
      ledOn=false;
      leds_off(LEDS_ALL);
    }
    while(current !=NULL){
      if( linkaddr_cmp(&(current->nextHop), &(current->addr)) &&  clock_seconds() - current->lastCom > 60L ){ // timeout of a child
        resetNetwork(); // trigger recomputation of the network
        current =NULL;
        continue;
      }
      current=current->next;
    }

    //printf("timer %d\n", myhop );
    // Delay 8-16 seconds 
    etimer_set(&et, CLOCK_SECOND * 10 + random_rand() % (CLOCK_SECOND * 2));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    
    if(myhop==2000){ // if tree is not builded
      resetNetwork(); // trigger recomputation 
      etimer_set(&et, CLOCK_SECOND * 30);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    }
    else if(!registered){ // if not registered, send reg message to parent
     
      registered = true;
      struct unicastMessage u;
      u.data = false; 
      u.reg = true;  // bool that make this a registration message 
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
    else{ // regular work, send random data
      struct unicastMessage u;
      u.data = true; 
      u.reg = false;
      u.temperature = random_rand(); // random data 
      if(u.temperature < 0){
	u.temperature = u.temperature * -1;      
      }
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
