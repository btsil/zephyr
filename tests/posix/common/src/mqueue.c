/*
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#define N_THR 2
#define STACKSZ (MAX(1024, PTHREAD_STACK_MIN) + CONFIG_TEST_EXTRA_STACK_SIZE)
#define SENDER_THREAD 0
#define RECEIVER_THREAD 1
#define MESSAGE_SIZE 16
#define MESG_COUNT_PERMQ 4

K_THREAD_STACK_ARRAY_DEFINE(stacks, N_THR, STACKSZ);

char queue[16] = "server";

char send_data[MESSAGE_SIZE] = "timed data send";

/*
 * For platforms that select CONFIG_KERNEL_COHERENCE, the receive buffer can
 * not be on the stack as the k_msgq that underlies the mq_timedsend() will
 * copy directly to the receiver's buffer when there is already a waiting
 * receiver.
 */

char rec_data[MESSAGE_SIZE];

void *sender_thread(void *p1)
{
	mqd_t mqd;
	struct timespec curtime;

	mqd = mq_open(queue, O_WRONLY);
	clock_gettime(CLOCK_MONOTONIC, &curtime);
	curtime.tv_sec += 1;
	zassert_false(mq_timedsend(mqd, send_data, MESSAGE_SIZE, 0, &curtime),
		      "Not able to send message in timer");
	usleep(USEC_PER_MSEC);
	zassert_false(mq_close(mqd),
		      "unable to close message queue descriptor.");
	pthread_exit(p1);
	return NULL;
}


void *receiver_thread(void *p1)
{
	mqd_t mqd;
	struct timespec curtime;

	mqd = mq_open(queue, O_RDONLY);
	clock_gettime(CLOCK_MONOTONIC, &curtime);
	curtime.tv_sec += 1;
	mq_timedreceive(mqd, rec_data, MESSAGE_SIZE, 0, &curtime);
	zassert_false(strcmp(rec_data, send_data), "Error in data reception");
	usleep(USEC_PER_MSEC);
	zassert_false(mq_close(mqd),
		      "unable to close message queue descriptor.");
	pthread_exit(p1);
	return NULL;
}

ZTEST(posix_apis, test_mqueue)
{
	mqd_t mqd;
	struct mq_attr attrs;
	int32_t mode = 0777, flags = O_RDWR | O_CREAT, ret, i;
	void *retval;
	pthread_attr_t attr[N_THR];
	pthread_t newthread[N_THR];

	attrs.mq_msgsize = MESSAGE_SIZE;
	attrs.mq_maxmsg = MESG_COUNT_PERMQ;

	mqd = mq_open(queue, flags, mode, &attrs);

	for (i = 0; i < N_THR; i++) {
		/* Creating threads */
		zassert_ok(pthread_attr_init(&attr[i]));
		pthread_attr_setstack(&attr[i], &stacks[i][0], STACKSZ);

		if (i % 2) {
			ret = pthread_create(&newthread[i], &attr[i],
					     sender_thread,
					     INT_TO_POINTER(i));
		} else {
			ret = pthread_create(&newthread[i], &attr[i],
					     receiver_thread,
					     INT_TO_POINTER(i));
		}

		zassert_false(ret, "Not enough space to create new thread");
		zassert_equal(pthread_attr_destroy(&attr[i]), 0);
	}

	usleep(USEC_PER_MSEC * 10U);

	for (i = 0; i < N_THR; i++) {
		pthread_join(newthread[i], &retval);
	}

	zassert_false(mq_close(mqd),
		      "unable to close message queue descriptor.");
	zassert_false(mq_unlink(queue), "Not able to unlink Queue");
}
