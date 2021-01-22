#define 		MYDEBUG 

#ifdef	MYDEBUG
#define 	isDebug     1
#define 	debug(x)    Serial.print(x)
#define 	debugln(x)  Serial.println(x)
#else
#define 	isDebug     0
#define 	debug(x)    {}
#define 	debugln(x)  {}
#endif

