#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "savresUtil.h"

#include "dbAccess.h"

/* $Id: savres.c,v 1.10 2011/08/17 00:39:49 saa Exp $ */

/* Simple tool to read/write array data from/to a file */

/* Author: Till Straumann <strauman@slac.stanford.edu>, 2006 */

/* undef only for testing; no true multi-threading is possible
 * w/o message queue (only a single aao supported).
 */
#define HAS_MSGQ

#ifndef NO_EPICS
#include <epicsThread.h>
#include <aaoRecord.h>
#include <dbCommon.h>
#include <dbLock.h>
#include <recSup.h>
#include <recGbl.h>
#include <assert.h>
#include <errlog.h>
#ifdef HAS_MSGQ
#include <epicsMessageQueue.h>
#else
#warning "no epicsMessageQueue; use for TESTING ONLY"
#include <epicsEvent.h>
static aaoRecord *theRec = 0;
#define epicsMessageQueueId epicsEventId
#define epicsMessageQueueCreate(nelms, size) epicsEventCreate(0)
#define epicsMessageQueueSend(q,x,s)         do { theRec = *(x); epicsEventSignal(q); } while (0)
#define epicsMessageQueueReceive(q, pm, s)   do { epicsEventWait(q); *(pm) = theRec;  } while (0) 
#endif
#endif

epicsMessageQueueId aaoSavResQId = 0;

static char *mkfnam(char *path, char *fnam)
{
char *s = malloc( ( path ? strlen(path) : 0 ) + strlen(fnam) + 2);

	if ( s ) {
		if ( path )
			sprintf(s,"%s/%s", path, fnam);
		else
			strcpy(s,fnam);
	}

	return s;
}

int
savresDumpData(char *path, char *fnam, char *buf, int n)
{
int  rval = -1;
char *s   = mkfnam(path,fnam);
int  fd   = -1;
int  put;
char *rbuf;

	if ( !s )
		return -1;

	if ( (fd=open(s,O_WRONLY|O_CREAT|O_TRUNC,0777)) < 0 ) {
		errlogPrintf("savresDumpData; unable to open file for writing: %s\n", strerror(errno));
		goto cleanup;
	}
	n   *= sizeof(*buf);
	rbuf = (char*)buf;
	while ( (put = write(fd, rbuf, n)) > 0 ) {
		n    -= put;
		rbuf += put;
	}

	if ( put < 0 ) {
		errlogPrintf("savresDumpData; error writing data: %s\n", strerror(errno));
		goto cleanup;
	}

	if ( n > 0 ) {
		errlogPrintf("savresDumpData; didn't write all data\n");
		goto cleanup;
	}

	rval = 0;

cleanup:
	if ( fd > -1 )
		close(fd);
	if ( rval ) {
		if ( unlink(s) ) {
			errlogPrintf("savresDumpData; WARNING: unable to remove bogus file: %s\n", strerror(errno));
		}
	}
	free(s);
	return rval;
}

int
savresRstrData(char *path, char *fnam, char *buf, int n)
{
int  rval = -1;
char *s   = mkfnam(path,fnam);
int  fd   = -1;
int  got,i;
char *rbuf;

	if ( !s )
		return -1;

	if ( (fd=open(s,O_RDONLY)) < 0 ) {
		errlogPrintf("savresRstrData; unable to open file for reading: %s\n", strerror(errno));
		goto cleanup;
	}

	i    = n*sizeof(*buf);
	rbuf = (char*)buf; 
	/* read(x,y,0) returns 0 */
	while ( (got = read(fd, rbuf, i)) > 0 ) {
		rbuf += got;
		i    -= got;
	}

	if ( got < 0 ) {
		errlogPrintf("savresRstrData; error reading data: %s\n", strerror(errno));
		goto cleanup;
	}

	rval = (rbuf - (char*)buf)/sizeof(*buf);
	
cleanup:
	free(s);
	if ( fd > -1 )
		close(fd);
	return rval;
}

#ifndef NO_EPICS

#define DEFAULT_PATH "/dat"

static int sizes[] = {  0 /* string */,
						sizeof(signed char),  sizeof(unsigned char), 
						sizeof(signed short), sizeof(unsigned short), 
						sizeof(signed long),  sizeof(unsigned long), 
						sizeof(float),        sizeof(double),
						/* xxx enum */ };

static char *gpath()
{
char             *path = getenv("DATA_PATH");
struct stat      sbuf;
	if ( !path && !stat(DEFAULT_PATH, &sbuf) && S_ISDIR(sbuf.st_mode) )
		path = DEFAULT_PATH;
	return path;
}


static void writer(void *arg)
{
struct aaoRecord *paao;
char             *path = gpath();

	do {
		epicsMessageQueueReceive( aaoSavResQId, &paao, sizeof(paao) );

		/* ignore invalid ftvl and write errors */
		if ( paao->ftvl > 0 && paao->ftvl < sizeof(sizes)/sizeof(sizes[0]) ) {
			savresDumpData(path, paao->name, paao->bptr, paao->nelm * sizes[paao->ftvl]);
		}

		/* aao can't do async processing :-( */
#if 0
		dbScanLock( (dbCommon*)paao );
		(*paao->rset->process)((dbCommon*)paao);
		dbScanUnlock( (dbCommon*)paao );
#endif
	} while (1);
}

/* schedule asynchronous dumping of record data;
 * record is processed again (2nd phase) when
 * writing completes.
 * 
 * This routine sets PACT.
 */
int
aaoDumpDataAsync(struct aaoRecord *paao)
{
int rval;
	rval = epicsMessageQueueSend( aaoSavResQId, &paao, sizeof(paao) );
	/* aao doesn't allow for async processing :-(.
	 * So we just asynchronously write the data out.
	 */
#if 0
	if ( id ) {
		paao->pact = TRUE;
	}
#endif
	return rval;
}

int
aaoRstrData(struct aaoRecord *paao)
{
int  rval;
char *path = gpath();

	/* try lazy init; this is usually called by single-threaded iocInit() during record init phase */
	if ( !aaoSavResQId )
		aaoSavResInit();

	rval = savresRstrData(path, paao->name, paao->bptr, paao->nelm*sizes[paao->ftvl]);
	if ( rval > 0 ) {
		paao->udf  = 0;
		recGblResetAlarms(paao);
	}
	return rval;
}

int 
aaoSavResInit()
{
	assert ( aaoSavResQId = epicsMessageQueueCreate(10, sizeof(struct aaoRecord*)) );
	assert ( epicsThreadCreate("aaoDataDumper", epicsThreadPriorityLow, epicsThreadGetStackSize(epicsThreadStackSmall), writer, 0) );
	return 0;
}

static long
link_helper(DBLINK *l, void *prec, int imax)
{
long c;

	if ( VME_IO != l->type ) {
		recGblRecordError(S_db_badField, (void*)prec, "(link_helper) Illegal OUT field (not VME_IO)");
		return -1;
	}
	if ( (c=l->value.vmeio.card) >= imax) {
		recGblRecordError(S_db_badField, (void*)prec, "(link_helper) card # too high");
		return -1;
	}
	return c;
}

long
savres_aao_init_record_helper(struct aaoRecord *paao, float *buf, int nelm, int ninst, int dim)
{
unsigned i,s;

	paao->ftvl = DBR_FLOAT;
	paao->nelm = nelm;

	if ( paao->bptr ) {
		recGblRecordError(S_db_badField, (void*)paao, "(savres_aao_init_record_helper) record already connected (database error)");
		return -1;
	}

	if ( (s=paao->out.value.vmeio.signal) >= ninst) {
		recGblRecordError(S_db_badField, (void*)paao, "(savres_aao_init_record_helper) signal # too high");
		return -1;
	}

	if ( buf ) {
		paao->bptr = buf + s*dim;
	} else if ( !(paao->bptr = malloc(sizeof(float) * dim) ) ) {
		recGblRecordError(
			S_db_noMemory, (void*)paao, "devAaoFdbk: No memory");
		paao->pact = 1;
		return -1;
	}

	for ( i = 0; i<dim; i++ ) {
		((float*)paao->bptr)[i] = (float)0.;
	}
	paao->val  = paao->bptr;
	/* use 'dpvt' to cache 'nelm' -- the aao record resets nelm
	 * if the record is written with an array < the original value
	 */

	paao->dpvt = (void*) (long)(paao->nord = paao->nelm);

    return(0);
}


long
savres_aao_init_record(struct aaoRecord *paao, SavresArrayIniDesc d, int dsz)
{
long rval;
int   c = link_helper( &paao->out, paao, dsz );
char *p;

	if ( c < 0 )
		return S_db_badField;


	rval = savres_aao_init_record_helper(paao, d[c].arr, d[c].nelm, d[c].ninst, d[c].dim);

	p =  paao->out.value.vmeio.parm;

	if ( !rval && ( !p || !strstr(p, "norest")) )
		aaoRstrData(paao);

	return rval;
}

static long
write_aao_pad(aaoRecord *paao)
{
int i;
	/* pad excess elements with zeroes */
	for ( i = paao->nelm; i < (long)paao->dpvt; i++ ) {
		((float*)paao->bptr)[i] = (float) 0.;
	}
	paao->nord = paao->nelm;
	paao->nelm = (long)paao->dpvt;
	return 0;
}

long
savres_aao_write(aaoRecord *paao)
{
long rval = write_aao_pad(paao);
char *p   = paao->out.value.vmeio.parm;

	if ( !rval && ( !p || !strstr(p,"norest")) )
		aaoDumpDataAsync(paao);

	return rval;
}

#endif

#ifdef TESTING
#define NN 10

float f1[NN];
float f2[NN];

static void rchk(char *path, char *fnam, float *orig, float *buf, int n, int nmax)
{
int i;
	for ( i=0; i<nmax; i++ )
		buf[i]=0.0;
	if ( savresRstrData(path,fnam, (char*)buf, nmax * sizeof(*buf)) < 0 ) {
		errlogPrintf("Restoring [%i] failure\n", n);
		exit(1);
	}
	for (i=0; i<n; i++) {
		if ( orig[i]!=buf[i] ) {
			errlogPrintf("Restoring [%i]; comparison failure @ %i\n", n, i);
			exit(1);
		}
	}
	for (i=n; i<nmax; i++) {
		if ( buf[i] != 0. ) {
			errlogPrintf("Restoring [%i]; zero comparison failure @ %i\n", n, i);
			exit(1);
		}
	}
}


int main(int argc, char **argv)
{
int i;
	if ( argc < 2 ) {
		fprintf(stderr,"Need <filename> arg\n");
		exit(2);
	}
	for ( i=0;  i<NN; i++ ) {
		f1[i]=(float)i;
	}
	if ( savresDumpData(0, argv[1], (char*)f1, NN*sizeof(f1[0])) ) {
		fprintf(stderr,"Saving failure\n");
		exit(1);
	}
	rchk(".",argv[1],f1,f2,NN,NN);
	if ( savresDumpData(0, argv[1], (char*)f1, 5*sizeof(f1[0])) ) {
		fprintf(stderr,"Saving failure\n");
		exit(1);
	}
	rchk(".",argv[1],f1,f2,5, NN);
}
#endif
