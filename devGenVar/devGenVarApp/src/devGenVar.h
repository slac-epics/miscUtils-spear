#ifndef DEV_GEN_VAR_H
#define DEV_GEN_VAR_H

#include <dbAddr.h>
#include <dbScan.h>
#include <dbCommon.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsTime.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic data structure describing a 'generic variable'.
 *
 * For each variable that is to be connected to the EPICS database
 * one DevGenVarRec must be created and initialized.
 *
 * 1) Allocate memory    (either statically or via malloc)
 * 2) Initialize memory: devGenVarInit( gv, num_elements );
 *    (devGenVarInit() can initialize an array of DevGenVarRec's
 *    in one sweep).
 * 3) Fill structure elements:
 *
 *       scan_p:   (optional) pointer to IOSCANPVT. If this is used
 *                 then you must initialize the IOSCANPVT object
 *                 yourself (scanIoInit())!. Note that multiple
 *                 DevGenVarRec's may point to the same scan-list
 *                 (which is to be initialized only once).
 *
 *                 If you initialize this field then your code may
 *                 cause the attached record(s) to scan by issuing
 *                 scanIoRequest() on the IOSCANPVT that scan_p is
 *                 pointing to. This causes all records connected
 *                 to all DevGenVarRec's pointing to the same IOSCANPVT
 *                 to process.
 * 
 *       mtx:      (optional) mutex used to synchronize access to the 
 *                 *data_p object. All access (read / write / read-modify-write)
 *                 of this device-support module acquires and releases
 *                 this mutex (if present).
 *
 *       evt:      (optional) event that can be used to notify
 *                 your code that associated record is done processing.
 *                 Posting this event can be suppressed if the flag bit
 *                 (1<<2) is set in the associated record's 'signal'
 *                 part of the INP/OUT link. This is useful if multiple
 *                 records are associated with a single GenVar. in such
 *                 a case it could make sense for only one record to send
 *                 an event.
 *
 *       data_p:   (mandatory) opaque pointer to your generic-variable.
 *
 *       dbr_t:    (mandatory) EPICS DBR type of the generic-varibale/object.               
 *
 *  Run-time fields:
 *       ts, stat, 
 *       sevr:     (optional) convey time-stamp, status + severity
 *                 back to input-record or asynchronous output record.
 *                 Output records write these fields (asynchronous records
 *                 write them during phase 1 and read them back during
 *                 phase 2).
 *
 *  Private fields:
 *       rec_p:    Used internally, initialize to NULL and do not modify.
 *
 *  NOTE: Only the mandatory and optional fields that you intend to use 
 *        need to be filled by you. Unused optional fields may remain
 *        as written by devGenVarInit().
 *
 *        After registering the structure with devGenVarRegister()
 *        the framework 'takes over' the structure, hence it *must not*
 *        live on the stack (local variable) or ever be 'free()ed'.
 */

typedef epicsEventId DevGenVarEvt; 
typedef epicsMutexId DevGenVarMtx;

typedef struct DevGenVarRec_ {
	IOSCANPVT      *scan_p;        /* scanlist (may be NULL)            */
	DevGenVarMtx    mtx;           /* protection (may be NULL)          */
	DevGenVarEvt    evt;           /* synchronization (may be NULL)     */
	volatile void  *data_p;        /* data we want to transfer          */
	unsigned        dbr_t;         /* DBR type of data we want to transfer from/to field */
	epicsTimeStamp  ts;            /* timestamp (if TSE == epicsTimeEventDeviceTime)     */
	epicsEnum16     stat, sevr;    /* status + severity                                  */
	dbCommon       *rec_p;         /* INTERNAL USE ONLY; DO NOT TOUCH                    */
} DevGenVarRec, *DevGenVar;

/*
 * Initialize an array of DevGenVarRec's. Must be called
 * before you set individual fields.
 * Static DevGenVarRec's may be initialized using the DEV_GEN_VAR_INIT()
 * macro below.
 */
static __inline__ void
devGenVarInit( DevGenVar p, int n_entries )
{
	memset( p, 0, n_entries * sizeof( *p ) );
}

/*
 * Initialize and array of DevGenVarRec's using devGenVarInit().
 * Then, create, initialize and attach an individual IOSCANPVT
 * object to each element's 'scan_p' pointer.
 * This is a helper for applications who want an individual 
 * scan-list for each variable.
 *
 * RETURNS: zero on success, nonzero if an error occurred (e.g., no memory).
 *          The state of the array is undefined if an error occurred.
 */
long
devGenVarInitScanPvt( DevGenVar p, int n_entries );

/*
 * Alternate initializer for static DevGenVarRec's. You should
 * always use this macro (in case more fields are added in the future)
 * in order to produce portable code. Initialize unused, optional fields
 * with 0.
 *
 * Example:
 *
 *    DevGenVarRec myVars[] = {
 *      DEV_GEN_VAR_INIT( &my_scanlist, 0, 0, &my_data,       DBR_LONG ),
 *      DEV_GEN_VAR_INIT( &my_scanlist, 0, 0, &my_other_data, DBR_LONG ),
 *    };
 *
 * In this example both variables are connected to the same scan-list.
 */
#define DEV_GEN_VAR_INIT( scan, mutx, evnt, data, type ) \
	{ scan_p: (scan), mtx: (mutx), evt: (evnt), data_p: (data), dbr_t: (type), \
      ts: { 0, 0 }, stat: 0, sevr: 0, rec_p: 0 }

/*
 * Register an array of DevGenVarRec's so that the device-support module
 * may find them.
 *
 * RETURNS: zero on success, nonzero on failure.
 *
 * NOTE   : The DevGenVarRec struct(s) that 'p' is referring to are
 *          'taken over' by the framework. It is a programming error to
 *          use local (stack) variables or 'free()' a DevGenVarRec
 *          after it has been passed to this routine. 
 */
long
devGenVarRegister(const char *registryEntry, DevGenVar p, int n_entries);

/*
 * Create an event and attach to 'p'. Always use this routine - the
 * underlying object may change in the future!
 * 
 */

long
devGenVarEvtCreate(DevGenVar p);

/* Block (with timeout) until devsup has written to generic variable
 * or is done reading from it.
 * Useful to synchronize low-level code with EPICS writing to an 
 * output record or reading from an input record.
 *
 * NOTES: Zero timeout returns immediately, negative timeout blocks
 *        indefinitely.
 *       
 *        ALWAYS use this routine, NEVER epicsEventWaitxxx() directly
 *        because the implementation may change, moving away from 
 *        epics events!
 *
 *        If multiple records are attached to the same DevGenVar
 *        then posting the event may be suppressed for individual
 *        records by setting bit (1<<2) in the INP/OUT link's 
 *        'signal' attribute.
 */
#define DEV_GEN_VAR_OK	       0
#define DEV_GEN_VAR_TIMEDOUT   1
#define DEV_GEN_VAR_ERRWAIT    2    /* blocking operation returned error    */
#define DEV_GEN_VAR_ERRNOEVT  -1	/* blocking not supported by this GenVar */

static __inline__ long
devGenVarWait(DevGenVar p, double timeout)
{
	if ( !p || !p->evt )
		return DEV_GEN_VAR_ERRNOEVT;

	return timeout < 0. ? 
	           epicsEventWait( p->evt )  :
               epicsEventWaitWithTimeout( p->evt, timeout );
}

/*
 * Create a lock and attach to 'p'. Always use this routine - the
 * underlying implementation may change in the future!
 */

long
devGenVarLockCreate(DevGenVar p);

/*
 * If you just want to create a lock (and attach to a DevGenVar
 * in a separate step then use devGenVarLockCreateRaw())
 */
static __inline__ DevGenVarMtx
devGenVarLockCreateRaw(void)
{
	return epicsMutexMustCreate();
}

static __inline__ void
devGenVarLockRaw(DevGenVarMtx mtx)
{
	epicsMutexMustLock( mtx );
}

static __inline__ void
devGenVarUnlockRaw(DevGenVarMtx mtx)
{
	epicsMutexUnlock( mtx );
}


/* Serialize access to underlying variable.
 * ALWAYS use these inlines - implementation of lock may change!
 */
static __inline__ void
devGenVarLock(DevGenVar p)
{
	if ( p->mtx )
		epicsMutexMustLock( p->mtx );
}

static __inline__ void
devGenVarUnlock(DevGenVar p)
{
	if ( p->mtx )
		epicsMutexUnlock( p->mtx );
}

static __inline__ void
devGenVarScan(DevGenVar p)
{
	if ( p->scan_p )
		scanIoRequest( *p->scan_p );
}

/* EPICS' 'general-purpose' hash table
 * is of limited size :-(
 * Call this *before* iocInit and *before*
 * the first call to devGenVarRegister() in
 * order to configure the table used by
 * devGenVar.
 * 
 * The 'ldTableSize' argument has to be
 * bigger or equal to 8 and less than or
 * equal to 16.
 *
 * RETURNS: Zero on success, nonzero
 * on failure (returning the current
 * table size).
 */
int
devGenVarConfig(unsigned ldTableSize);

/*
 * Complete asynchronous processing (for records/devsup
 * that support this).
 * Allows for propagating a timestamp back to the record:
 *
 *  1) record processes first time
 *  2) write value to genVar
 *  3) notify ll-code via event
 *  4) set pact and complete first processing step
 *  5) ll-code uses value
 *  6) ll-code sets timestamp, stat + severity
 *  7) ll-code calls devGenVarProcComplete();
 *  8) record processes second time, sets timestamp
 *     (if TSE == epicsTimeEventDeviceTime), stat + sevr
 *  9) done.
 */

int
devGenVarProcComplete(DevGenVar p);

#ifdef __cplusplus
}
#endif

#endif
