/* -*- C -*- */
/*
 * COPYRIGHT 2014 SEAGATE LLC
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF SEAGATE LLC,
 * ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF SEAGATE TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF SEAGATE LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF SEAGATE'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A SEAGATE REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include "c0appz.h"
#include "helpers/helpers.h"
#include "clovis/clovis.h"
#include "clovis/clovis_internal.h"
#include "clovis/clovis_idx.h"
#include "lib/thread.h"
#include <json-c/json.h>
#include <sys/time.h>	
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h> 
#include <sys/syscall.h>
#define KLEN 256
#define VLEN 256
#define VALINPUT 512
#define MAXVAL 70000 
#define CNT 1 

struct kvsns_xattr{
	unsigned long long int ino;
	char type;
	char name[256];
}__attribute((packed));


#define XATTR_KEY_INIT(key, ino2, xname)   \
{                                               \
        key->ino = ino2;	\
	key->type = '7';	\
	memset(key->name, 0, 256);				\
        memcpy(key->name, xname, strlen(xname));\
}


static struct m0_fid ifid;
static struct m0_ufid_generator kvsns_ufid_generator;
static struct m0_clovis_idx idx;
struct m0_bufvec key[100];
struct m0_bufvec val[100];
struct m0_clovis_op *op_arr[100];

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int done = 0;

static void callback(struct m0_clovis_op *op)
{
	//printf("\n In callback and done %d", done);
	//printf("(Callback) The thread id is %u \n", (unsigned int) syscall( __NR_gettid ));
	/* Check rcs array even if op is succesful */
	int rcs[1];
	int rc = rcs[0];
	if (rc)
	{
		printf("\nerror(%d):rcs array", rc);
		goto out;
	}

out:
	pthread_mutex_lock(&mutex);

	done++;
	if (done == 100)
		pthread_cond_signal(&cond);

	pthread_mutex_unlock(&mutex);

	//m0_semaphore_up(&sem);

}

static int  m0_op_kvs_async(enum m0_clovis_idx_opcode opcode, struct m0_bufvec *key, struct m0_bufvec *val, struct m0_clovis_op **op)
{
	int rc;
	int *rcs;
	rcs = (int*) m0_alloc(sizeof(int));

	//M0_ASSERT(0);
	struct m0_clovis_idx     *index = NULL;
	struct m0_clovis_op_ops   op_ops;

	//printf("\nval to be fetched %d\n", val->ov_vec.v_count[0]);
	index = &idx;
	rc = m0_clovis_idx_op(index, opcode, key, val, rcs, M0_OIF_OVERWRITE, op);

	if (rc)
	{
               printf("\nerror(%d): m0_clovis_idx_op", rc); 
	       return rc;
	}
	op_ops.oop_executed = NULL;
	op_ops.oop_stable = callback;
	op_ops.oop_failed = callback;

	m0_clovis_op_setup(*op, &op_ops, 0);

	//printf("Before launch thread id is %u \n", (unsigned int) syscall( __NR_gettid ));
	m0_clovis_op_launch(op, 1);

	return rc;
}

static int setAttr()
{
	char *k1 = "1name_of_key";
	char *v1 = malloc(sizeof(char)* VALINPUT);

	struct kvsns_xattr *xkey[100];
	size_t klen, vlen;
	int rc, i;

	unsigned long long int ino2;

	memset(v1, '*', VALINPUT);
	ino2 = atoll("123456");

	char tmpkey[256];
	struct timeval start1, end1;

	gettimeofday(&start1, NULL);

	klen = sizeof(struct kvsns_xattr);
	vlen = strlen(v1) + 1;

	for (i = 0; i < 100; i ++)
	{
		xkey[i] = calloc(1, sizeof( struct kvsns_xattr));
                snprintf(tmpkey, 256, "%s_%d", k1, i);
                XATTR_KEY_INIT(xkey[i], ino2, tmpkey);

		rc = m0_bufvec_alloc(&key[i], 1, klen);
		if (rc != 0)
			printf("error(%d): m0_bufvec_alloc", rc);

		rc = m0_bufvec_alloc(&val[i], 1, vlen);
		if (rc != 0)
			printf("error(%d): m0_bufvec_alloc", rc);

		memcpy(key[i].ov_buf[0], xkey[i], klen);

		memcpy(val[i].ov_buf[0], v1, vlen);

		m0_op_kvs_async(M0_CLOVIS_IC_PUT, &key[i], &val[i], &op_arr[i]);
	}

/*	for (i = 0; i < 100; i++)
	{
		m0_op_kvs_async(M0_CLOVIS_IC_PUT, &key[i], &val[i], &op_arr[i]);
	//	m0_semaphore_down(&sem);
	//	done--;
	}
*/
	while (done < 100)
	{
		pthread_cond_wait( & cond, & mutex );
	}

	printf("\ncurrent thread main");
	for (i = 0; i < 100; i++)
	{ 
		m0_clovis_op_fini(op_arr[i]);
		free(xkey[i]);
		m0_bufvec_free(&key[i]);
		m0_bufvec_free(&val[i]);
	}
	gettimeofday(&end1, NULL);
	timer(start1, end1, "stored 100 keys one at a time async");

	return rc;
}

int set_fid()
{
	 char  tmpfid[255];
	int rc = 0;

	// Get fid from config parameter 
         memset(&ifid, 0, sizeof(struct m0_fid));
         rc = m0_fid_sscanf("<0x780000000000000b:1>", &ifid);
         if (rc != 0) {
                 fprintf(stderr, "Failed to read ifid value from conf\n");
                 goto err_exit;
         }

         rc = m0_fid_print(tmpfid, 255, &ifid);
         if (rc < 0) {
                 fprintf(stderr, "Failed to read ifid value from conf\n");
                 goto err_exit;
         }

         m0_clovis_idx_init(&idx, &clovis_container.co_realm,
                            (struct m0_uint128 *)&ifid);

         rc = m0_ufid_init(clovis_instance, &kvsns_ufid_generator);
         if (rc != 0) {
	 fprintf(stderr, "Failed to initialise fid generator: %d\n", rc);
                 goto err_exit;
         }

         return 0;

 err_exit:
         return rc;
}

void timer(struct timeval start1, struct timeval end1, char *msg)
{
	long mtime, secs, usecs;
	secs  = end1.tv_sec  - start1.tv_sec;
	usecs = end1.tv_usec - start1.tv_usec;
	mtime = ((secs) * 1000 + usecs/1000.0) + 0.5;
 	printf("Elapsed time for %s: %ld millisecs\n", msg, mtime);

}
	
/* main */
int main(int argc, char **argv)
{
	/* check input */
	if (argc != 4) {
		fprintf(stderr,"Usage:\n");
		fprintf(stderr,"%s key value\n", basename(argv[0]));
		return -1;
	}

	/* time in */
	c0appz_timein();

	/* c0rcfile
	 * overwrite .cappzrc to a .[app]rc file.
	 */
	char str[256];
	sprintf(str, ".%src", basename(argv[0]));
	c0appz_setrc(str);
	c0appz_putrc();

	/* initialize resources */
	if (c0appz_init(0) != 0) {
		fprintf(stderr,"error! clovis initialization failed.\n");
		return -2;
	}

	c0appz_timeout(0);
	c0appz_timein();

 	rc = set_fid();
	if (rc != 0)
		fprintf(stderr, "error in fid initialization");	
	
//	m0_semaphore_init(&sem, 0);

	setAttr();
//	m0_semaphore_fini(&sem);
	printf("\n after setattr called\n");

/* free resources*/
	c0appz_free();

	/* time out */
	fprintf(stderr, "%4s", "free");
	c0appz_timeout(0);

	/* success */
	fprintf(stderr, "%s success\n", basename(argv[0]));
	return 0;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
