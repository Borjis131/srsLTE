/*
 * Copyright 2020 Universitat Politecnica de Valencia
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#ifndef SYNC_QUEUE_H
#define SYNC_QUEUE_H

#include <iostream>
#include <boost/intrusive/list.hpp>
#include <pthread.h>
#include <time.h>

#include "srslte/interfaces/enb_interfaces.h"
#include "srslte/upper/sync.h"

/***********************************************
 * Multiple producer/consumer thread-safe queue.
 * Using linked list to enable fast sorting.
 * 
 * All implemented in a header file to avoid C++
 * problems with templates.
 * ********************************************/

template <typename Data>
class sync_queue{

private:
    boost::intrusive::list<Data, boost::intrusive::constant_time_size<false>> queue;
    pthread_mutex_t mutex;
    pthread_cond_t cv_has_data;
    uint32_t max_check_intervals;
    srsenb::pdcp_interface_gtpu* pdcp = nullptr;
    uint32_t consumer = 0;
    uint32_t producer = 0;

public:
    explicit sync_queue<Data>(srsenb::pdcp_interface_gtpu* pdcp_, int checks_ = 4){
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&cv_has_data, NULL);
        max_check_intervals = checks_;
        pdcp = pdcp_;
    }

    sync_queue<Data>(){
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&cv_has_data, NULL);
        max_check_intervals = 0;
        pdcp = nullptr;
    }

    /*
     * push():
     * pushes the data and sorts the queue
     * notifies the waiting thread
     */
    void push(Data &data){
        consumer++;
        std::cout << "producer waiting for lock (push)" << unsigned(consumer) << "\n";
        pthread_mutex_lock(&mutex);
        std::cout << "producer lock obtained" << unsigned(consumer) << "\n";
        queue.push_back(data);
        //std::cout << "Pushed element\n";
        usort();
        pthread_cond_signal(&cv_has_data);
        std::cout << "producer fired condition variable signal" << unsigned(consumer) << "\n";
        pthread_mutex_unlock(&mutex);
        std::cout << "producer lock released" << unsigned(consumer) << "\n";
    }

    /*
     * usort():
     * sorts the list according to std::less()
     * not thread-safe
     */
    void usort(){
        queue.sort();
        //std::cout << "Queue sorted\n";
    }

    /*
     * sort():
     * sorts the list according to std::less()
     * thread-safe
     */
    void sort(){
        pthread_mutex_lock(&mutex);
        queue.sort();
        pthread_mutex_unlock(&mutex);
        //std::cout << "Queue sorted\n";
    }

    /*
     * empty():
     * returns true queue is empty
     */
    bool empty() const{
        pthread_mutex_lock(&mutex);
        bool ret = queue.empty();
        pthread_mutex_unlock(&mutex);
        return ret;
    }

    /*
     * try_pop():
     * tries to pop one element if queue its
     * not empty
     */
    bool try_pop(Data &popped_value, int lcid_counter){
        pthread_mutex_lock(&mutex);
        if(queue.empty()){
            //std::cout << "Queue has no elements\n";
            pthread_mutex_unlock(&mutex);
            return false;
        }
        popped_value = std::move(queue.front());
        queue.pop_front();
        //std::cout << "Popped element: " << popped_value << "\n";
        pthread_mutex_unlock(&mutex);

        // Check how to access to payload right here
        pdcp->write_sdu(0xFFFD, lcid_counter, std::move(popped_value.payload));
        return true;
    }

    /*
     * wait_and_pop():
     * pops one element from the queue, waiting
     * if the queue is empty. Running in continuous loop.
     */
    void wait_and_pop(/*Data &popped_value*/){
        std::cout << "SYNC consumer started\n";
        Data popped_value;
        while(true){
            producer++;
            std::cout << "consumer waiting for lock (wait_and_pop)" << unsigned(producer) << "\n";
            pthread_mutex_lock(&mutex);
            std::cout << "consumer obtained lock" << unsigned(producer) << "\n";
            while(queue.empty()){
                std::cout << "consumer waiting for cv" << unsigned(producer) << "\n";
                pthread_cond_wait(&cv_has_data, &mutex);
                std::cout << "consumer obtained cv" << unsigned(producer) << "\n";
            }
            popped_value = std::move(queue.front());
            //popped_value = queue.front();
            queue.pop_front();
            //std::cout << "Popped element: " << popped_value << "\n";
            pthread_mutex_unlock(&mutex);
            std::cout << "consumer released lock" << unsigned(producer) << "\n";
            // lcid_counter hardcoded to 1 for now
            pdcp->write_sdu(0xFFFD, 1, std::move(popped_value.payload));
        }
    }

    /*
     * wait_time_and_pop():
     * waits the time specified and pops one element 
     * from the queue. Running in continuous loop.
     */
    void wait_time_and_pop(/*Data &popped_value*/){
        std::cout << "SYNC consumer started\n";
        Data popped_value;
        while(true){
            pthread_mutex_lock(&mutex);
            uint32_t num_of_checks = max_check_intervals; // Default 4
            while(queue.empty()){
                pthread_cond_wait(&cv_has_data, &mutex); // Wait for the conditional variable in push function
            }

            timespec pop_time = perform_checks(popped_value, num_of_checks);
            // previously set to CLOCK_MONOTONIC
            clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &pop_time, (timespec *)NULL);
            //nanosleep(&pop_time, (timespec *)NULL);

            timespec now;
            // previously set to CLOCK_MONOTONIC
            clock_gettime(CLOCK_REALTIME, &now);
            //std::cout << "Popped element: " << popped_value << " at: " << pop_time.tv_nsec - now.tv_nsec << "\n";
            queue.pop_front();

            pthread_mutex_unlock(&mutex);
        }
    }
    /*
    * perform_checks(): 
    * performs num_of_checks-1 checks while waiting for the Data
    * to be delivered. Checks the queue to see if new packets need 
    * to be delivered previously.
    */
    timespec perform_checks(Data &popped_value, int num_of_checks){
        popped_value = std::move(queue.front()); // Check front of the queue
        uint16_t timestamp = popped_value.get_timestamp();
        timespec pop_time = uint16_to_timespec(timestamp);
        timespec now;
        // previously set to CLOCK_MONOTONIC
        clock_gettime(CLOCK_REALTIME, &now);

        timespec wait_time = ts_difftime(pop_time, now);

        if(wait_time.tv_sec < 0 || (wait_time.tv_sec <= 0 && (wait_time.tv_nsec - 500000L < 0L))){ // Adjusts minimum time to perform several checks
            //std::cout << "First if: Time to wait is less than 0.5ms, popping inmediatly\n";
            return pop_time;
        }

        timespec wait_interval;
        wait_interval.tv_sec = wait_time.tv_sec / num_of_checks;
        wait_interval.tv_nsec = wait_time.tv_nsec / num_of_checks;

        //std::cout << "Wait interval: " << wait_interval.tv_sec << ":" << wait_interval.tv_nsec << " to pop -> " << popped_value << "\n";
        while(num_of_checks > 1){
            pthread_mutex_unlock(&mutex);
            //std::cout << "Checking: " << num_of_checks << "\n";

            nanosleep(&wait_interval, (timespec *)NULL);

            pthread_mutex_lock(&mutex);
            
            wait_time = ts_difftime(wait_time, wait_interval);

            //std::cout << "Time remaining: " << wait_time.tv_sec << ":" << wait_time.tv_nsec << "\n";
            if(wait_time.tv_sec < 0 || (wait_time.tv_sec <= 0 && (wait_time.tv_nsec - 500000L < 0L))){ // Adjusts minimum time to perform several checks
                //std::cout << "Second if: Time to wait is less than 0.5ms, popping inmediatly\n";
                return pop_time;
            }

            num_of_checks--;

            if(popped_value != std::move(queue.front())){ // If the current value to pop is not the front of the queue (reordering)
                //std::cout << "Value to pop is not the front of the queue\n";
                return perform_checks(popped_value, num_of_checks);
            }
        }
        return pop_time;
    }

    /*
    * pthread_wrapper():
    * wraps the function called by queue->function()
    * to work with pthread_create
    */
    static void* pthread_wrapper(void* s_queue){
        sync_queue<Data>* queue = static_cast<sync_queue<Data>*>(s_queue);
        queue->wait_and_pop();
        return NULL;
    }

    /*
    * ts_difftime():
    * subtract timespec y to timespec x
    * returns the difference
    */
    timespec ts_difftime(timespec x, timespec y){
        timespec result;
        if(x.tv_nsec < y.tv_nsec){
            long seconds = (y.tv_nsec - x.tv_nsec) / 1000000000L + 1;
            y.tv_nsec -= 1000000000L * seconds;
            y.tv_sec += seconds;
        } if(x.tv_nsec - y.tv_nsec > 1000000000){
            int seconds = (x.tv_nsec - y.tv_nsec) / 1000000000L;
            y.tv_nsec += 1000000000L * seconds;
            y.tv_sec -= seconds;
        }
        /* Compute the time remaining. tv_nsec is certainly positive. */
        result.tv_sec = x.tv_sec - y.tv_sec;
        result.tv_nsec = x.tv_nsec - y.tv_nsec;

        return result;
    }

    /*
    * ts_addtime():
    * add timespec y to timespec x
    * returns the sum
    */
   timespec ts_addtime(timespec x, timespec y){
       timespec result;
       if(x.tv_nsec + y.tv_nsec >= 1000000000L){
            result.tv_nsec = x.tv_nsec + y.tv_nsec - 1000000000L;
            result.tv_sec = x.tv_sec + y.tv_sec +1;
       } else {
            result.tv_sec = x.tv_sec + y.tv_sec;
            result.tv_nsec = x.tv_nsec + y.tv_nsec;
       }

       return result;
   }
    
    /*
    * uint16_to_timespec():
    * turns uint16_t timestamp into timespec
    * timestamp value represent multiples of 10 ms
    */
    timespec uint16_to_timespec(uint16_t timestamp){
        timespec conversion;
        // timestamp * 10 = milliseconds
        if(timestamp >= 100){
            conversion.tv_nsec = (timestamp%100)*10000000;
            conversion.tv_sec = timestamp/100;
        } else {
            conversion.tv_nsec = timestamp*10000000;
            conversion.tv_sec = 0;
        }
        return conversion;
    }
};

#endif // SYNC_QUEUE_H