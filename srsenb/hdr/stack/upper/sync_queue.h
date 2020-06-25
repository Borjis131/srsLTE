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
#include <algorithm>

#include "srslte/interfaces/enb_interfaces.h"
#include "srslte/upper/sync.h"

/***********************************************
 * Multiple producer/consumer thread-safe queue.
 * Using linked list to enable fast sorting.
 * 
 * All implemented in a header file to avoid C++
 * problems with templates.
 * ********************************************/
template <typename Data, typename Info>
class sync_queue{

private:
    boost::intrusive::list<Data, boost::intrusive::constant_time_size<false>> data_queue; // Queue storing sync data packets
    boost::intrusive::list<Info, boost::intrusive::constant_time_size<false>> info_queue; // Queue storing sync info packets
    pthread_mutex_t data_mutex, info_mutex;
    pthread_cond_t cv_has_data, cv_has_info;
    int max_check_intervals;
    srsenb::pdcp_interface_gtpu* pdcp = nullptr;
    uint32_t consumer = 0;
    uint32_t producer = 0;
    timespec sync_period = {}; // Stores the sync period reference
    int sync_period_counter = 0; // Number of sync periods
    int sequence_duration; // Duration in ms of the SYNC sequence
    //int sort_elements = 10; // Number of elements to be sorted with partial_sort

public:
    explicit sync_queue<Data, Info>(srsenb::pdcp_interface_gtpu* pdcp_, int checks_ = 4, int seq_duration_ = 10){
        pthread_mutex_init(&data_mutex, NULL);
        pthread_mutex_init(&info_mutex, NULL);
        pthread_cond_init(&cv_has_data, NULL);
        pthread_cond_init(&cv_has_info, NULL);
        max_check_intervals = checks_; // Add parameter
        sequence_duration = seq_duration_;
        pdcp = pdcp_;
    }

    sync_queue<Data, Info>(){
        pthread_mutex_init(&data_mutex, NULL);
        pthread_mutex_init(&info_mutex, NULL);
        pthread_cond_init(&cv_has_data, NULL);
        pthread_cond_init(&cv_has_info, NULL);
        max_check_intervals = 0;
        sequence_duration = 0;
        pdcp = nullptr;
    }

    /*
     * set_sync_period()
     * sets the sync period reference for the relative timestamps
     * currently waits for the info_mutex (check)
     */
    void set_sync_period(uint32_t sync_period_sec, uint32_t sync_period_nsec){
        //std::cout << "Received sync period seconds: " << sync_period_sec << " and nanoseconds: " << sync_period_nsec << "\n";
        pthread_mutex_lock(&info_mutex);
        sync_period.tv_sec = sync_period_sec;
        sync_period.tv_nsec = sync_period_nsec;
        pthread_mutex_unlock(&info_mutex);
    }

    /*
     * push_data():
     * push function for MBMS data packets
     * notifies the waiting thread
     */
    void push_data(Data &data){
        pthread_mutex_lock(&data_mutex);
        consumer++;
        std::cout << "producer waiting for lock (push_data)" << unsigned(consumer) << "\n";
        std::cout << "producer lock obtained" << unsigned(consumer) << "\n";
        data_queue.push_back(data);
        data_queue.sort();
        // partial_sort_data();
        pthread_cond_signal(&cv_has_data);
        std::cout << "producer fired condition variable signal" << unsigned(consumer) << "\n";
        pthread_mutex_unlock(&data_mutex);
        std::cout << "producer lock released (push_data)\n";
    }

    /*
     * push_info():
     * push function for MBMS synchronisation information packets
     * sorts the info_queue and notifies the waiting thread
     */
    void push_info(Info &info){
        pthread_mutex_lock(&info_mutex);
        std::cout << "producer waiting for lock (push_info)\n";
        std::cout << "producer lock obtained (push_info)\n";
        info_queue.push_back(info);
        info_queue.sort();
        // partial_sort_info();
        pthread_cond_signal(&cv_has_info);
        std::cout << "producer fired condition variable signal (push_info)\n";
        pthread_mutex_unlock(&info_mutex);
        std::cout << "producer lock released (push_info)\n";
    }

    /*
     * sort_data():
     * sorts the data list according to std::less()
     * thread-safe
     */
    void sort_data(){
        pthread_mutex_lock(&data_mutex);
        data_queue.sort();
        pthread_mutex_unlock(&data_mutex);
    }

    /*
     * TODO: implement partial_sort for data_packets
     * Not thread-safe
     */
    void partial_sort_data(){}

    /*
     * TODO: implement partial_sort for info_packets
     * Not thread-safe
     */
    void partial_sort_info(){}

    /*
     * wait_and_pop():
     * pops one element from the info_queue and all the stored elements in data_queue.
     * Waits if the queue is empty. Runs in continuous loop.
     */
    void wait_and_pop(){
        Info popped_value = {};
        Data popped_data_value = {};
        while(true){
            producer++;
            std::cout << "consumer waiting for info_lock (wait_and_pop)" << unsigned(producer) << "\n";
            pthread_mutex_lock(&info_mutex);
            std::cout << "consumer obtained info_lock" << unsigned(producer) << "\n";
            while(info_queue.empty()){
                std::cout << "consumer waiting for info_cv" << unsigned(producer) << "\n";
                pthread_cond_wait(&cv_has_info, &info_mutex);
                std::cout << "consumer obtained info_cv" << unsigned(producer) << "\n";
            }
            popped_value = info_queue.front();
            info_queue.pop_front();
            pthread_mutex_unlock(&info_mutex);
            std::cout << "consumer released info_lock\n";
            std::cout << "consumer waiting for data_lock\n";
            pthread_mutex_lock(&data_mutex);
            std::cout << "consumer obtained data_lock" << unsigned(producer) << "\n";
            while(data_queue.empty()){
                pthread_cond_wait(&cv_has_data, &data_mutex);
            }

            int data_burst = 0;
            int queue_size = data_queue.size();

            for(int i = 0; i < queue_size; i++){
                popped_data_value = std::move(data_queue.front());
                if(popped_value.get_timestamp() == popped_data_value.get_timestamp()){
                    data_burst++;
                    data_queue.pop_front();
                    pdcp->write_sdu(0xFFFD, 1, std::move(popped_data_value.payload)); // Hardcoded lcid for now
                } else {
                    data_queue.pop_front();
                    data_queue.push_front(popped_data_value);
                    break;
                }
            }
            std::cout << data_burst << " data packets sent\n";
            pthread_mutex_unlock(&data_mutex);
            std::cout << "consumer released lock" << unsigned(producer) << "\n";
        }
    }

    /*
     * wait_time_and_pop():
     * waits the time specified by timestamp and pops all elements 
     * with the same timestamp from the info_queue and data_queue. 
     * Running in continuous loop.
     */
    void wait_time_and_pop(){
        Info popped_value = {};
        Data popped_data_value[50]; // Just a random number ideally the SYNC sequence max_value
        while(true){
            producer++;
            std::cout << "consumer waiting for info_lock (wait_time_and_pop)" << unsigned(producer) << "\n";
            pthread_mutex_lock(&info_mutex);
            std::cout << "consumer obtained info_lock" << unsigned(producer) << "\n";
            //uint32_t num_of_checks = max_check_intervals; // Default 4
            while(info_queue.empty()){
                std::cout << "consumer waiting for info_cv" << unsigned(producer) << "\n";
                pthread_cond_wait(&cv_has_info, &info_mutex); // Wait for the conditional variable in push_info function
                std::cout << "consumer obtained info_cv" << unsigned(producer) << "\n";
            }

            timespec pop_time = perform_checks(popped_value, max_check_intervals); // previously num_of_checks
            
            // Retrieve all sequence packets before the last "check"
            popped_value = info_queue.front();
            info_queue.pop_front();
            pthread_mutex_unlock(&info_mutex);
            std::cout << "consumer released info_lock\n";

            std::cout << "consumer waiting for data_lock\n";
            pthread_mutex_lock(&data_mutex);
            while(data_queue.empty()){
                pthread_cond_wait(&cv_has_data, &data_mutex);
            }
            std::cout << "consumer obtained data_lock\n";

            // partial_sort_data(); // Sort here the queue?
            
            int data_burst = 0;
            int queue_size = data_queue.size();
            timespec now, delay_data, delay_info;

            // The limit is the number of items in the queue but the loop breaks in the first different timestamp
            for(int i = 0; i < queue_size; i++){
                popped_data_value[i] = std::move(data_queue.front());
                if(popped_value.get_timestamp() == popped_data_value[i].get_timestamp()){
                    data_burst++;
                    data_queue.pop_front();
                    // DEBUG
                    std::cout << "(inside if) checking unique_byte_buffer_t: " << popped_data_value[i].payload.N_bytes << "\n";
                    //std::cout << "Popped data element at: " << delay_data.tv_sec << " seconds and " << delay_data.tv_nsec << " nanoseconds\n";
                    //pdcp->write_sdu(0xFFFD, 1, std::move(popped_data_value.payload)); // Hardcoded lcid for now
                } else {
                    data_queue.pop_front();
                    data_queue.push_front(popped_data_value[i]);
                    break;
                }
            }
            std::cout << data_burst << " in data_burst\n";
            pthread_mutex_unlock(&data_mutex);
            std::cout << "consumer released lock" << unsigned(producer) << "\n";

            // Perform the last "check"
            clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &pop_time, (timespec *)NULL);

            clock_gettime(CLOCK_REALTIME, &now);
            delay_info = ts_difftime(pop_time, now);
            std::cout << "Popped info element at: " << delay_info.tv_sec << " seconds and " << delay_info.tv_nsec << " nanoseconds\n";

            for(int i = 0; i < data_burst; i++){
                clock_gettime(CLOCK_REALTIME, &now);
                delay_data = ts_difftime(pop_time, now);
                std::cout << "Popped data element at: " << delay_data.tv_sec << " seconds and " << delay_data.tv_nsec << " nanoseconds\n";
                // DEBUG
                std::cout << "(inside for) checking unique_byte_buffer_t: " << popped_data_value[i].payload.N_bytes << "\n";
                pdcp->write_sdu(0xFFFD, 1, std::move(popped_data_value[i].payload)); // Hardcoded lcid for now
            }
        }
    }

    /*
    * perform_checks(): 
    * performs max_checks-1 checks while waiting for the Info packet
    * to be delivered. Checks the info_queue to see if new packets need 
    * to be delivered previously.
    */
    timespec perform_checks(Info &popped_value, int max_checks){
        int current_checks = max_checks; // To store the maximum checks for this iteration
        popped_value = info_queue.front(); // Check front of the info queue
        uint16_t timestamp = popped_value.get_timestamp();

        std::cout << "perform_checks timestamp: " << unsigned(timestamp) << "\n";

        // Update SYNC period every timestamp 0
        if(timestamp == 0){
            std::cout << "Updating sync_period reference\n";

            sync_period.tv_sec = sync_period.tv_sec + (((60000*sequence_duration)/1000)*sync_period_counter); //60000 or 59999?
            sync_period_counter++;
        }

        timespec pop_time = timestamp_to_timespec(timestamp); // 0 timestamp and sequence_duration*ms case solved
        pop_time = ts_addtime(pop_time, sync_period); // Adding the period to the timestamp
        
        timespec check_reference; // Current time, used for adding the wait_interval every loop
        clock_gettime(CLOCK_REALTIME, &check_reference);
        timespec wait_time = ts_difftime(pop_time, check_reference); // Full wait time
        
        std::cout << "Overall checks wait_time: " << wait_time.tv_sec << ":" << wait_time.tv_nsec << ", reference: " << check_reference.tv_sec << ":" 
        << check_reference.tv_nsec << " sync_period: " << sync_period_counter << " and pop_time: " << pop_time.tv_sec << ":" << pop_time.tv_nsec << "\n";
        //std::cout << "Current sync_period seconds: " << current_sync_period.tv_sec << " and nanoseconds: " << current_sync_period.tv_nsec << "\n";
        
        if(wait_time.tv_sec < 0 || (wait_time.tv_sec <= 0 && (wait_time.tv_nsec - 500000L < 0L))){ // Adjusts minimum time to perform several checks
            std::cout << "First if: Time to wait is less than 0.5ms, popping inmediatly\n";
            return pop_time;
        }

        timespec wait_interval;
        wait_interval.tv_sec = wait_time.tv_sec / max_checks;
        wait_interval.tv_nsec = wait_time.tv_nsec / max_checks;
        std::cout << "Wait interval " << wait_interval.tv_sec << " seconds and " << wait_interval.tv_nsec << " nanoseconds\n";

        timespec sleep_interval;
        long long mult;
        while(current_checks > 1){
            pthread_mutex_unlock(&info_mutex);
            mult = wait_interval.tv_sec*1000000000 + wait_interval.tv_nsec;
            mult = mult * (max_checks - current_checks + 1); // This adds the number of wait intervals needed to reach the absolute time

            // To timespec again
            sleep_interval.tv_sec = mult / 1000000000;
            sleep_interval.tv_nsec = mult % 1000000000;

            sleep_interval = ts_addtime(sleep_interval, check_reference); // Add the check_reference for the absolute time
            clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &sleep_interval, (timespec *)NULL);

            pthread_mutex_lock(&info_mutex);

            wait_time = ts_difftime(wait_time, wait_interval);

            if(wait_time.tv_sec < 0 || (wait_time.tv_sec <= 0 && (wait_time.tv_nsec - 500000L < 0L))){ // Adjusts minimum time to perform several checks
                std::cout << "Second if: Time to wait is less than 0.5ms, popping inmediatly\n";
                return pop_time;
            }

            current_checks--;
            
            if(popped_value != info_queue.front()){ // If the current value to pop is not the front of the queue (reordering)
                return perform_checks(popped_value, current_checks);
            }
        }
        return pop_time;
    }

    /*
    * sync_consumer():
    * wraps the function called by queue->function()
    * to work with pthread_create
    */
    static void* sync_consumer(void* s_queue){
        sync_queue<Data, Info>* queue = static_cast<sync_queue<Data, Info>*>(s_queue);
        //queue->wait_and_pop();
        queue->wait_time_and_pop();
        return NULL;
    }

    /*
    * ts_difftime():
    * subtract timespec y to timespec x
    * returns the difference
    */
    timespec ts_difftime(timespec x, timespec y){
        timespec result;
        if(x.tv_sec > y.tv_sec && x.tv_nsec > y.tv_nsec){
            result.tv_sec = x.tv_sec - y.tv_sec;
            result.tv_nsec = x.tv_nsec - y.tv_nsec;
        }else if(x.tv_sec > y.tv_sec && x.tv_nsec < y.tv_nsec){
            result.tv_sec = x.tv_sec - y.tv_sec - 1;
            result.tv_nsec = 1000000000L + (x.tv_nsec - y.tv_nsec);
        }else if(x.tv_sec < y.tv_sec && x.tv_nsec > y.tv_nsec){
            result.tv_sec = x.tv_sec + 1 - y.tv_sec;
            result.tv_nsec = x.tv_nsec - y.tv_nsec - 1000000000L;
        }else{
            result.tv_sec = -(y.tv_sec - x.tv_sec);
            result.tv_nsec = -(y.tv_nsec - x.tv_nsec);
        }
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
            result.tv_sec = x.tv_sec + y.tv_sec + 1;
       } else {
            result.tv_sec = x.tv_sec + y.tv_sec;
            result.tv_nsec = x.tv_nsec + y.tv_nsec;
       }
       return result;
   }
    
    /*
    * timestamp_to_timespec():
    * turns uint16_t SYNC timestamp into timespec
    * SYNC timestamp value represent multiples of 10 ms
    * starting the first one in 0 but representing 10 ms
    * this function takes that into account
    */
    timespec timestamp_to_timespec(uint16_t timestamp){
        timespec conversion;
        timestamp += 1; // Adding 1 to all timestamps
        // Considering timestamp 0 represents sequence_length milliseconds and so on
        int ratio = (1000/sequence_duration);
        if(timestamp >= ratio){
            conversion.tv_nsec = (timestamp%ratio)*(1000000000/ratio);
            conversion.tv_sec = timestamp/ratio;
        } else {
            conversion.tv_nsec = timestamp*(1000000000/ratio);
            conversion.tv_sec = 0;
        }
        return conversion;
    }
};

#endif // SYNC_QUEUE_H