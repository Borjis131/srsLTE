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
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/chrono.hpp>

#include "srslte/upper/sync.h"

/***********************************************
 * Multiple producer/consumer thread-safe queue.
 * Using linked list to enable fast sorting.
 * 
 * All implemented in a header file to avoid C++
 * problems with templates.
 * ********************************************/

template<typename Data>
class sync_queue{

private:
    boost::intrusive::list<Data, boost::intrusive::constant_time_size<false>> queue;
    mutable boost::mutex mutex;
    boost::condition_variable has_data;
    uint32_t max_check_intervals = 4; // Performs max_check_intervals-1 "normal" checks and waits for the last

public:

    /*
     * push():
     * pushes the data and sorts the queue
     * notifies the waiting thread
     */
    void push(Data& data){
        boost::mutex::scoped_lock lock(mutex);
        queue.push_back(data);
        std::cout << "pushing address: " << &data << "\n";
        std::cout << "timestamp: " << data.get_timestamp() << "\n";
        //std::cout << "Element pushed: " << data << "\n";
        sort();
        lock.unlock(); // unlocks the mutex
        has_data.notify_one();
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
        boost::mutex::scoped_lock lock(mutex);
        return queue.empty();
    }

    /*
     * try_pop():
     * tries to pop one element if queue its
     * not empty
     */
    bool try_pop(Data& popped_value){
        boost::mutex::scoped_lock lock(mutex);
        if(queue.empty()){
            std::cout << "Queue has no elements\n";
            return false;
        }
        popped_value=queue.front();
        queue.pop_front();
        std::cout << "Popped element: " << popped_value << "\n";
        return true;
    }

    /*
     * wait_and_pop():
     * pops one element from the queue, waiting
     * if the queue is empty
     */
    void wait_and_pop(Data& popped_value){
        boost::mutex::scoped_lock lock(mutex);
        while(queue.empty()){
            has_data.wait(lock);
        }
        popped_value = queue.front();
        std::cout << "popping address: " << &popped_value << "\n";
        std::cout << "popping: " << unsigned(popped_value.pdu_type) << "\n";
        std::cout << "timestamp: " << popped_value.get_timestamp() << "\n";
        
        queue.pop_front();
        //std::cout << "Popped element: " << popped_value << "\n";
    }

    /*
     * wait_time_and_pop():
     * waits the time specified and pops one element 
     * from the queue, waiting if the queue is empty
     */
    void wait_time_and_pop(Data& popped_value, bool running){
        std::cout << "SYNC consumer started\n";
        // TODO: Continuous loop, tear down when eNB running flag is false
        while(running){
            boost::mutex::scoped_lock lock(mutex);
            uint32_t num_of_checks = max_check_intervals; // Default 4
            while(queue.empty()){
                has_data.wait(lock); // Wait for the conditional lock in push function
            }
            
            boost::chrono::system_clock::time_point pop_time = perform_checks(popped_value, num_of_checks);
            
            //std::cout << "Popped element: " << popped_value << " at: " << pop_time - boost::chrono::system_clock::now() << "\n";

            // Compensate delay by substracting - boost::chrono::nanoseconds(100000) to pop_time

            boost::this_thread::sleep_until(pop_time - boost::chrono::nanoseconds(100000));
            std::cout << "Popped element: " << popped_value << " at: " << pop_time - boost::chrono::system_clock::now() << "\n";
            queue.pop_front();
            //std::cout << "Popped element: " << popped_value << " at: " << pop_time - boost::chrono::system_clock::now() << "\n";
        }
    }
    /*
    * perform_checks(): 
    * performs num_of_checks-1 checks while waiting for the Data
    * to be delivered. Checks the queue to see if new packets need 
    * to be delivered previously.
    */
    boost::chrono::system_clock::time_point perform_checks(Data& popped_value, int num_of_checks){
        popped_value = queue.front(); // Check front of the queue
        boost::chrono::system_clock::time_point pop_time = popped_value.get_timestamp();
        boost::chrono::system_clock::duration wait_time = pop_time - boost::chrono::system_clock::now();

        if(wait_time - boost::chrono::nanoseconds(500000) < boost::chrono::nanoseconds(0)){ // Adjusts minimum time to perform several checks
            std::cout << "First if: Time to wait is less than 0.5ms, popping inmediatly\n";
            return pop_time;
        }

        boost::chrono::system_clock::duration wait_interval = wait_time/num_of_checks;

        std::cout << "Time to wait: " << wait_time << " to pop: "<< popped_value << "\n";
        while(num_of_checks > 1){
            mutex.unlock();
            std::cout << "Checking: " << num_of_checks << "\n";

            // boost::this_thread::sleep_for(wait_interval);
            
            boost::this_thread::sleep_until(pop_time - (wait_interval*(num_of_checks-1)));
            
            mutex.lock();

            wait_time = pop_time - boost::chrono::system_clock::now();
            std::cout << "Time remaining: " << wait_time << "\n";
            if(wait_time - boost::chrono::nanoseconds(500000) < boost::chrono::nanoseconds(0)){ // Adjusts minimum time to perform several checks
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
};

#endif // SYNC_QUEUE_H