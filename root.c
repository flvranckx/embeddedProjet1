

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
  bool metered;
  struct data *head;
  int dataCount;
};
struct data{
  int value;
  struct data * next;
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
  

  }
}
static void
recv_uc(struct unicast_conn *c, const linkaddr_t *from)
{

  struct unicastMessage * u = packetbuf_dataptr();

  struct rootingTable * current= myRootingTable;
  while(current!=NULL){
    if(current->addr.u8[0] == from->u8[0] && current->addr.u8[1] == from->u8[1]){
      current->lastCom = clock_seconds();
      
    }
    current=current->next;
  }

  if(u->reg){
    

  
    addToRootingTable(u->from_addr, *from);
    leds_off(LEDS_ALL);
  }
  if(u->dest_addr.u8[0] == from->u8[0] && u-> dest_addr.u8[1] == from->u8[1]){

    return; 
  }

  if(linkaddr_cmp(&nullAddr, &(u->dest_addr))){
    
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

        resetNetwork();
      }
      else{

        unicast_send(&uc, &(current->nextHop));
        
      }
      struct data * new = malloc(sizeof(struct data));
      new->value=u->temperature;
      new->next=current->head;
      current->head = new;
      current->dataCount++;

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
        i++;
      }
      current->dataCount=0;
      double a;
      double b;
      //linreg(10,x,y,&a,&b,NULL);
      
      if(a>0.0){

        struct unicastMessage u3;
        u3.data = true; 
        u3.reg = false;
        u3.temperature = -1;
        u3.from_addr = linkaddr_node_addr;
        u3.dest_addr = current->addr;
        
        packetbuf_copyfrom(&u3, sizeof(struct unicastMessage));
        unicast_send(&uc, &(current->nextHop));
      }
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

    }
  }
  
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
