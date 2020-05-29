
// this file is for a computation node, it should work very similar to a regular node. 

#define REAL double
#include "contiki.h"
#include "random.h"
#include "net/rime/rime.h"
#include <stdbool.h> 

#include <stdio.h> /* For printf() */
#include <stdlib.h>
#include "dev/leds.h"



#include <math.h>       


unsigned int myhop=2000; // what distance am I from the root ? if 2000, the tree is not builded yet 
unsigned int nextHop0; // legacy code, before I found the linkaddr_t data type. I used two unsigned int to store an address.
unsigned int nextHop1; 
unsigned int reset_hopLimit;
unsigned long ledTimer;
bool ledOn = false;

unsigned int node_metered=0; // the amount of node from which the data will be computed localy and not sended to the root. max 5 
unsigned long lastAck; // last ack received from the root. Will send keep-alive periodically
static struct etimer et;
bool registered =false; // am I in the tree ? 
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
bool isMetered(linkaddr_t *a); // check if a specific address is computed from this computation node. 




static REAL sqr(REAL x) { // for the regression. 
    return x*x;
}

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{

  struct broadcastMessage * b = packetbuf_dataptr();
  printf("broadcast message received from %d.%d: '%d'\n",
         from->u8[0], from->u8[1], b->hop);
  if(b->reset){ // crital call for a recomputation of the tree
    if(myhop != 2000){
      myhop = 2000;
      resetNetwork();

    }
  }
  else if(b->hop < myhop){ // if this one is closer than the closest I already had. 
    myhop = b->hop;
    nextHop0 = from->u8[0]; // set next hop in the tree
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
  printf("node received a message from %d via %d for %d with data %d\n", u->from_addr.u8[0], from->u8[0], u->dest_addr.u8[0], u->temperature);
  
  struct rootingTable * current= myRootingTable;
  while(current!=NULL){ // handle timeout for disconnection. we expect all the direct child node to send data once per minute. 
    if(current->addr.u8[0] == from->u8[0] && current->addr.u8[1] == from->u8[1]){
      current->lastCom = clock_seconds();
      
    }
    current=current->next;
  }

  if(u->reg){ // registration of child node
    
    printf("received reg message from %d.%d\n", from->u8[0],from->u8[1]);
  
    addToRootingTable(u->from_addr, *from);

  }
  if(linkaddr_cmp(&linkaddr_node_addr, &(u->dest_addr))){
    if(u->temperature==-1){ // special instruction to indicate that a node needs to open the valve. 
      ledOn=true;
      leds_on(LEDS_ALL);
      ledTimer = clock_seconds()+10;
    }
    
    lastAck = clock_seconds();
    return; 
  }
  
  if(linkaddr_cmp(&nullAddr, &(u->dest_addr))){ // null address are for the root of the tree. Can be intercepted by this node as a computation node
    if(!isMetered(from)){ // if we reached the maximum sensor for this calculation, transfer it
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
      if(current==NULL){//Rooting table error, recomputation of the network is needed
        
        resetNetwork();
      }
      else{
      
        unicast_send(&uc, &(current->nextHop)); // send ack 
        
      }
      struct data * new = malloc(sizeof(struct data)); // store data
      new->value=u->temperature;
      new->next=current->head;
      current->head = new;
      current->dataCount++;
    
    if(current->dataCount == 10 ){ // if we have enough data
      
      double y[10];
      double x[10];
      int i = 1;
      while(i<11){ // would not compile with a for loop, I did not look any further into it. I didn't have time to waste on this
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
      linreg(10,x,y,&a,&b,NULL); // do the computation
      if(a>0.0){

        struct unicastMessage u3;
        u3.data = true; 
        u3.reg = false;
        u3.temperature = -1; // special code, it means "open the valve"
        u3.from_addr = linkaddr_node_addr;
        u3.dest_addr = current->addr;
        
        packetbuf_copyfrom(&u3, sizeof(struct unicastMessage));
        unicast_send(&uc, &(current->nextHop)); // send to the node from who we processed the data
      }

    }
  }

  else{ // if the message is not for this node, transfer to the next hop 
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
bool isMetered(linkaddr_t *a){ // is that a node we make the calculation for ? 
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
  if(linkaddr_cmp(dest, &linkaddr_null)) { // legacy debug, I don't dare to delete
    return;
  }

}
void addToRootingTable(linkaddr_t addr, linkaddr_t nexthop){ // registration of a child node. 
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
void resetNetwork(){ // trigger this function to start the recomputation of the network. 
  struct broadcastMessage b;
  b.reset = true;
  b.hop = 0;
  packetbuf_copyfrom(&b, sizeof(struct broadcastMessage));
  broadcast_send(&broadcast);

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

  broadcast_open(&broadcast, 129, &broadcast_call);
  unicast_open(&uc, 146, &unicast_callbacks);
  while(1) {
    struct rootingTable * current = myRootingTable;
    if( clock_seconds() -  lastAck >60L){ //timeout from root
      resetNetwork(); // trigger recomputation
      continue;
    }
    if(ledOn && ledTimer < clock_seconds()){ // close valve, it has been open for 10 sec 
      ledOn = false;
      leds_off(LEDS_ALL);
     
    }
    while(current !=NULL){
      if( linkaddr_cmp(&(current->nextHop), &(current->addr)) &&  clock_seconds() - current->lastCom > 60L ){ //check for timeout of child
        resetNetwork(); // trigger recomputation
        current =NULL;
        continue;
      }
      current=current->next;
    }
  
    //printf("timer %d\n", myhop );
    // Delay 8-16 seconds 
    etimer_set(&et, CLOCK_SECOND * 10 + random_rand() % (CLOCK_SECOND * 2)); // should act every 10 sec plus a bit of randomness for collision avoidence. 

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    
    if(myhop==2000){ // if not in a tree, trigger the recomputation
      resetNetwork(); 
      etimer_set(&et, CLOCK_SECOND * 30);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    }
    else if(!registered){ // if in a tree but not registered to parent node
     
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
    
    else{ // if every normal, send keep-alive to root. 
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
int linreg(int n, const REAL x[], const REAL y[], REAL* m, REAL* b, REAL* r){ // least square fit
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
