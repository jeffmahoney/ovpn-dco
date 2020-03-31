/*
 *  OVPN -- OpenVPN protocol accelerator for Linux
 *  Copyright (C) 2012-2020 OpenVPN Technologies, Inc.
 *  All rights reserved.
 *  Author: James Yonan <james@openvpn.net>
 */

#ifndef _NET_OVPN_DCO_OVPNTIMER_H_
#define _NET_OVPN_DCO_OVPNTIMER_H_

/*
 * A timeout intended to be triggered after a time
 * period in which zero events occurred.
 * Used for keepalive.
 */
struct ovpn_timer
{
	unsigned long period;     /* configured time period (relative jiffies) */
	unsigned long revisit;    /* next check to extend expiration (absolute jiffies) */
	struct timer_list timer;
};


/*
 * A helper macro to retreive the structure the ovpn_timer is embedded in from
 * its timer member
 */
#define from_ovpn_timer(var, callback_timer, ovpn_timer_fieldname) \
    container_of(container_of((struct timer_list*)callback_timer, struct ovpn_timer, timer), \
                 typeof(*var), ovpn_timer_fieldname)


/*
 * When should we mod timer again?
 */
static inline unsigned long __ovpn_timer_next_revisit(void)
{
	unsigned long rv = jiffies + HZ;
	if (sizeof(rv) < 8 && unlikely(!rv))
		rv = 1;
	return rv;
}

/*
 * Indicate that an event occurred.
 * Called from softirq only.
 */
static inline void ovpn_timer_event(struct ovpn_timer *t)
{
	const unsigned long rv = READ_ONCE(t->revisit);

	/* as an optimization, only update the timer periodically */
	if (rv && unlikely(time_after_eq(jiffies, rv))) {
		/* schedule next timer adjustment */
		WRITE_ONCE(t->revisit, __ovpn_timer_next_revisit());

		/* will not re-activate and modify already deleted timers */
		mod_timer_pending(&t->timer, jiffies + READ_ONCE(t->period));
	}
}

/*
 * Schedule or reschedule the timer.
 * Called from process context or softirq.
 * Returns 0 if timer was scheduled, or 1 otherwise
 * (such as if timer is already scheduled or deleted).
 */
static inline int ovpn_timer_schedule(struct ovpn_timer *t,
				      spinlock_t *lock)
{
	int ret = 1;
	unsigned long period;

	spin_lock_bh(lock); /* prevent race between schedule and delete */
	period = t->period;
	if (period) {
		t->revisit = __ovpn_timer_next_revisit();
		ret = mod_timer(&t->timer, jiffies + period);
	}
	spin_unlock_bh(lock);
	return ret;
}

/*
 * Initialize the timer.
 * Called from process context.
 */
static inline void ovpn_timer_init(struct ovpn_timer *t,
				   void (*fn)(struct timer_list *))
{
	t->period = 0;
	t->revisit = 0;
	timer_setup(&t->timer, fn, 0);
}

/*
 * Set the period of the timer in seconds.
 */
static inline void ovpn_timer_set_period(struct ovpn_timer *t,
					 unsigned int seconds)
{
	WRITE_ONCE(t->period, (unsigned long) seconds * HZ);
}

/*
 * Delete the timer.
 * Called from process context.
 * Returns 1 if the timer was deleted, 0 otherwise.
 */
static inline int ovpn_timer_delete(struct ovpn_timer *t,
				    spinlock_t *lock)
{
	int ret = 0;
	spin_lock_bh(lock);  /* prevent race between schedule and delete */
	t->period = 0;
	t->revisit = 0;
	ret = del_timer(&t->timer);
	spin_unlock_bh(lock);
	return ret;
}

#endif /* _NET_OVPN_DCO_OVPNTIMER_H_ */