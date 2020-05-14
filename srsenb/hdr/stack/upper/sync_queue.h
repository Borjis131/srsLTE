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

public:
    explicit sync_queue<Data>(int checks_ = 4){
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&cv_has_data, NULL);
        max_check_intervals = checks_;
    }

    /*
     * push():
     * pushes the data and sorts the queue
     * notifies the waiting thread
     */
    void push(Data &data){
        pthread_mutex_lock(&mutex);
        queue.push_back(data);
        std::cout << "Pushed element\n";
        //std::cout << "timestamp: " << data.get_timestamp() << "\n";
        //std::cout << "Element pushed: " << data << "\n";
        sort();
        pthread_cond_signal(&cv_has_data);
        pthread_mutex_unlock(&mutex);
    }

    /*
     * sort():
     * sorts the list according to timestamp
     */
    void sort(){
        queue.sort();
        std::cout << "Queue sorted\n";
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
    bool try_pop(Data &popped_value){
        pthread_mutex_lock(&mutex);
        if(queue.empty()){
            std::cout << "Queue has no elements\n";
            pthread_mutex_unlock(&mutex);
            return false;
        }
        popped_value = queue.front();
        queue.pop_front();
        std::cout << "Popped element: " << popped_value << "\n";
        pthread_mutex_unlock(&mutex);
        return true;
    }

    /*
     * wait_and_pop():
     * pops one element from the queue, waiting
     * if the queue is empty
     */
    void wait_and_pop(Data &popped_value){
        pthread_mutex_lock(&mutex);
        while(queue.empty()){
            pthread_cond_wait(&cv_has_data, &mutex);
        }
        popped_value = queue.front();
        std::cout << "popping: " << unsigned(popped_value.pdu_type) << "\n";
        //std::cout << "timestamp: " << popped_value.get_timestamp() << "\n";

        queue.pop_front();
        //std::cout << "Popped element: " << popped_value << "\n";
        pthread_mutex_unlock(&mutex);
    }

    /*
     * wait_time_and_pop():
     * waits the time specified and pops one element 
     * from the queue, waiting if the queue is empty
     */
    void wait_time_and_pop(Data &popped_value, bool running){
        //std::cout << "SYNC consumer started\n";
        // TODO: Continuous loop, tear down when eNB running flag is false
        while(running){
            pthread_mutex_lock(&mutex);
            uint32_t num_of_checks = max_check_intervals; // Default 4
            while(queue.empty()){
                pthread_cond_wait(&cv_has_data, &mutex); // Wait for the conditional lock in push function
            }

            //boost::chrono::system_clock::time_point pop_time = perform_checks(popped_value, num_of_checks);
            timespec pop_time = perform_checks(popped_value, num_of_checks);

            //std::cout << "Popped element: " << popped_value << " at: " << pop_time - boost::chrono::system_clock::now() << "\n";

            // Compensate delay by substracting - boost::chrono::nanoseconds(100000) to pop_time
            //boost::this_thread::sleep_until(pop_time - boost::chrono::nanoseconds(100000));

            /*
            timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            pop_time.tv_sec = pop_time.tv_sec - now.tv_sec;
            pop_time.tv_nsec = pop_time.tv_nsec - now.tv_nsec;*/

            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &pop_time, (timespec *)NULL);
            //nanosleep(&pop_time, (timespec *)NULL);

            timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            std::cout << "Popped element: " << popped_value << " at: " << pop_time.tv_nsec - now.tv_nsec << "\n";
            queue.pop_front();

            //std::cout << "Popped element: " << popped_value << " at: " << pop_time - boost::chrono::system_clock::now() << "\n";
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
        popped_value = queue.front(); // Check front of the queue
        timespec pop_time = popped_value.get_timestamp();
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        timespec wait_time = ts_difftime(pop_time, now);

        if(wait_time.tv_sec == 0 && (wait_time.tv_nsec - 500000L < 0L)){ // Adjusts minimum time to perform several checks
            std::cout << "First if: Time to wait is less than 0.5ms, popping inmediatly\n";
            return pop_time;
        }

        timespec wait_interval;
        wait_interval.tv_sec = wait_time.tv_sec / num_of_checks;
        wait_interval.tv_nsec = wait_time.tv_nsec / num_of_checks;

        std::cout << "Wait interval: " << wait_time.tv_sec << ":" << wait_time.tv_nsec << " to pop -> " << popped_value << "\n";
        while(num_of_checks > 1){
            pthread_mutex_unlock(&mutex);
            std::cout << "Checking: " << num_of_checks << "\n";
            //boost::this_thread::sleep_for(wait_interval);
            nanosleep(&wait_interval, (timespec *)NULL);
            //boost::this_thread::sleep_until(pop_time - (wait_interval*(num_of_checks-1)));

            pthread_mutex_lock(&mutex);

            //wait_time = pop_time - boost::chrono::system_clock::now();
            /*
            clock_gettime(CLOCK_MONOTONIC, &now);
            wait_time.tv_sec = pop_time.tv_sec - now.tv_sec;
            wait_time.tv_nsec = pop_time.tv_nsec - now.tv_nsec;*/
            
            wait_time = ts_difftime(wait_time, wait_interval);

            std::cout << "Time remaining: " << wait_time.tv_sec << ":" << wait_time.tv_nsec << "\n";
            if(wait_time.tv_sec == 0 && (wait_time.tv_nsec - 500000L < 0L)){ // Adjusts minimum time to perform several checks
                std::cout << "Second if: Time to wait is less than 0.5ms, popping inmediatly\n";
                return pop_time;
            }

            num_of_checks--;

            if(popped_value != queue.front()){ // If the current value to pop is not the front of the queue (reordering)
                std::cout << "Value to pop is not the front of the queue\n";
                return perform_checks(popped_value, num_of_checks);
            }
        }
        return pop_time;
    }

    timespec ts_difftime(timespec x, timespec y){
        timespec result;
        if(x.tv_nsec < y.tv_nsec){
            long seconds = (y.tv_nsec - x.tv_nsec) / 1000000000 + 1;
            y.tv_nsec -= 1000000000 * seconds;
            y.tv_sec += seconds;
        } if(x.tv_nsec - y.tv_nsec > 1000000000){
            int seconds = (x.tv_nsec - y.tv_nsec) / 1000000000;
            y.tv_nsec += 1000000000 * seconds;
            y.tv_sec -= seconds;
        }
        /* Compute the time remaining to wait. tv_nsec is certainly positive. */
        result.tv_sec = x.tv_sec - y.tv_sec;
        result.tv_nsec = x.tv_nsec - y.tv_nsec;

        return result;
    }
};

#endif // SYNC_QUEUE_H