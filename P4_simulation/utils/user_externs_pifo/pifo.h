
//////////////////////////////////////////////////// todo list
//////////////////////////////////////////////////// 1) force dequeue and error correction are not unsigned short introduced properly yet.
//////////////////////////////////////////////////// 2) add the arrival time in each packet (I am using the pkt_ptr instead in the enqueue_FS now)

#ifndef SIMPLE_SWITCH_PSA_DIV_H_
#define SIMPLE_SWITCH_PSA_DIV_H_
#include <bm/bm_sim/logger.h>
#include <bm/bm_sim/extern.h>
#define _STDC_WANT_LIB_EXT1_ 1
#define _CRT_SECURE_NO_WARNINGS
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <map>
#include <numeric>
#include <chrono>
#include <iostream>
#include <queue>
#include <ctime>

using namespace std::chrono;
#pragma once

namespace bm {

// if the rank = 0, that means this level is not used (pkts will be handled in a FIFO order in this level), the lowest rank in any level is "1".

// this is the main class of the pifo scheduler that will be used later in any usage of the scheduler.
class pifo_scheduler : public bm::ExternType {
 public:
  BM_EXTERN_ATTRIBUTES {
    BM_EXTERN_ATTRIBUTE_ADD(verbose);
  }


void init() override {  // Attributes
  static constexpr std::uint32_t QUIET = 0u;
  // Init variables
  verbose_ = verbose.get<std::uint32_t>() != QUIET;};

// the packet struct that presents any packet inside the scheduler.
	struct packet {
		unsigned int level3_flow_id;
		unsigned int flow_id;
		unsigned int rank;
		unsigned int pred;
		unsigned int pkt_ptr;
		std::vector<unsigned int> levels_ranks;
	};
// The flow scheduler struct : which is a queue that sorts 1 head packet from each flow.
	struct flow_scheduler {
		std::shared_ptr<packet> object;
		std::shared_ptr<flow_scheduler> next;
	};
// The Fifo bank struct : which consists of multiple FIFO queues, each one is dedicated to one flow, and stores all packets from this flow except the head packet.
	struct fifo_bank {
		unsigned int flow_id;
		std::shared_ptr<packet> object;
		std::shared_ptr<fifo_bank> bottom;
		std::shared_ptr<fifo_bank> left;
	};

	static unsigned int time_now; // the current time, increment by 1 each time we call the scheduler for a dequeue (which is continous)

// level 3 of the hierarchy variables
	static std::vector<unsigned int> number_of_queues_per_level;
	static std::vector<unsigned int> number_of_pkts_per_queue_each_level;


	static std::vector<std::shared_ptr<flow_scheduler>> FS; 

	static std::vector<std::shared_ptr<fifo_bank>> FB; // the fifo bank queues, each flow scheduler has its own FIFO bank which stores the rest of packets of the flow handled in this flow scheduler.

// level 2 of the hierarchy variables

// level 1 of the hierarchy variables (root)
	 

	std::shared_ptr<packet> deq_packet_ptr = NULL; // the pointer to the dequeued packet
	static unsigned int number_of_enqueue_packets; // the total number of enqueued packets until now.

	static unsigned int number_of_read_packets; // the total number of captured packets by the TM_buffer.h until now.

	static unsigned int number_of_dequeue_packets; // the total number of dequeued packets until now.

	static unsigned int switch_is_ready; 

// these variables will contain the inputs that will be inserted by the user, to be used later. 
	unsigned int flow_id;
	static std::vector<unsigned int> pkt_levels_ranks;

	unsigned int pred;
	unsigned int enq;
	static unsigned int pkt_ptr;  //new static
	unsigned int deq;
	static std::queue<unsigned int> pkt_ptr_queue;

	static int start_time; 
	static int last_time; 

	void pass_rank_values(const Data& rank_value, const Data& level_id)
	{
		pkt_levels_ranks.erase(pkt_levels_ranks.begin() + level_id.get<uint32_t>());
		pkt_levels_ranks.insert(pkt_levels_ranks.begin() + level_id.get<uint32_t>(), rank_value.get<uint32_t>());
	}

	void my_scheduler(const Data& in_flow_id, const Data& idle_time, const Data& in_enq, const Data& in_pkt_ptr, const Data& reset_time)
	{

// copy the inputs values :: Todo : they should be removed later and just use the inputs directly.
	
		if(reset_time.get<uint32_t>() == 1)
		{
			time_now = 0;
		}
		
		flow_id = in_flow_id.get<uint32_t>();
		pred = idle_time.get<uint32_t>();
		enq = in_enq.get<uint32_t>();
		pkt_ptr = in_pkt_ptr.get<uint32_t>();
		pkt_ptr_queue.push(pkt_ptr);
		deq = 0;

// the core code of the pifo scheduler, that enqueue, dequeue or force dequeue packets.
		run_core();
	}

// the function for enqueue/dequeue to/from the third level of the hierarchy.

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void level_controller(std::shared_ptr<packet>& level_packet_ptr, unsigned int level_enq, unsigned int level_deq, unsigned int level_id)
	{
		std::shared_ptr<flow_scheduler> head_FS = NULL;
		std::shared_ptr<packet> out_deq_pkt_ptr;
		std::shared_ptr<fifo_bank> head_FB =  NULL;
		unsigned int queue_id = 0;
		unsigned int sum_number_all_queues = 0;

		for(int i = 0; i < int(level_id); i++)
		{
			sum_number_all_queues = sum_number_all_queues + number_of_queues_per_level[i];

		}
		if (level_enq == 1)
		{	

			if(level_id == 0)
			{
				head_FB = FB[queue_id];
			}

			head_FS = FS[queue_id + sum_number_all_queues];

			pifo(level_packet_ptr, level_enq, level_deq, \
			head_FS, head_FB, \
			time_now, out_deq_pkt_ptr);

			FS[queue_id + sum_number_all_queues] = head_FS;
			if(level_id == 0)
			{
				FB[queue_id] = head_FB;	
			}
		}
		else if (level_deq == 1)
		{
			if(level_id == 0)
			{
				head_FB = FB[queue_id];
			}

			pifo(level_packet_ptr, level_enq, level_deq, \
			FS[queue_id + sum_number_all_queues], head_FB, \
			time_now, out_deq_pkt_ptr);

			level_packet_ptr = out_deq_pkt_ptr;
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// the core function of the pifo scheduler, applies enqueue and dequeue operations, to & from each level of the hirarchy, 
//and each level is responsible of enqueue and dequeue in each queue inside this level 
	void run_core()
	{
		deq_packet_ptr = NULL;

		if (enq == 1)
		{

			if(start_time == 0)
			{
				//start_time = std::time(0);
				start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

			}

			number_of_enqueue_packets = number_of_enqueue_packets + 1;
			std::shared_ptr<packet> enq_packet_ptr;
			enq_packet_ptr = std::make_shared<packet>();
			enq_packet_ptr->level3_flow_id = flow_id;
			enq_packet_ptr->flow_id = flow_id;
			enq_packet_ptr->rank = pkt_levels_ranks[0];
			enq_packet_ptr->pred = pred;
			enq_packet_ptr->pkt_ptr = pkt_ptr;
			enq_packet_ptr->levels_ranks = pkt_levels_ranks;


			level_controller(enq_packet_ptr, enq, 0, 0);

		}

		if ((deq == 1)&&(switch_is_ready == 1))
		{
			//last_time = std::time(0);
			last_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

			if(last_time > start_time)
			{
				time_now = last_time - start_time;
			}
			else
			{
				time_now = 0;
			}

			//std::this_thread::sleep_for(std::chrono::microseconds(810));  // equivalent to 1ms (with adding the overhead of the code)
			std::this_thread::sleep_for(std::chrono::microseconds(10));  // equivalent to 1ms (with adding the overhead of the code)

			deq_packet_ptr = std::shared_ptr<packet>(std::make_shared<packet>());

			level_controller(deq_packet_ptr, 0, deq, 0);
			std::shared_ptr<packet> intermediate_pkt_ptr = std::shared_ptr<packet>(std::make_shared<packet>());
		}	
	}


// This is the pifo queue function which handles 1 flow_scheduler and 1 FIFO at a time, this function is used by each level function
	void pifo(std::shared_ptr<pifo_scheduler::packet> pkt_ptr, unsigned int in_enq, unsigned int in_deq, std::shared_ptr<pifo_scheduler::flow_scheduler>& in_head_FS, std::shared_ptr<pifo_scheduler::fifo_bank>& in_head_FB, \
		unsigned int in_time_now, std::shared_ptr<packet>& out_deq_pkt_ptr)
	{
		std::shared_ptr<pifo_scheduler::packet> deq_packet_ptr = NULL;
		std::shared_ptr<pifo_scheduler::flow_scheduler> cur_ptr_FS;

// in case of enqueue (enq ==1), a packet will be enqueued to flow scheduler first enqueue_FS (if its flow already existed there), it will be enqueued in the FIFO bank instead.
// in case of dequeue (deq ==1), a packet will be dequeued from the flow scheduler dequeue_FS, then the next packet from the same flow will be dequeued from the FIFO bank dequeue_FB,
// then enqueue this next packet to the flow scheduler enqueue_FS.
		if (in_enq == 1)
		{
			std::shared_ptr<pifo_scheduler::packet> enq_packet_ptr;
			enq_packet_ptr = std::shared_ptr<pifo_scheduler::packet>(std::make_shared<pifo_scheduler::packet>(*pkt_ptr));

			cur_ptr_FS = in_head_FS;

			enqueue_FB(enq_packet_ptr, in_head_FB);

			enqueue_FS(enq_packet_ptr, in_head_FS);
		}
		if (in_deq == 1)
		{			
			dequeue_FS(deq_packet_ptr, in_head_FS, in_time_now);

			if (deq_packet_ptr != NULL)
			{
				std::shared_ptr<pifo_scheduler::packet> FB_FS_enq_packet_ptr;
				FB_FS_enq_packet_ptr = std::shared_ptr<pifo_scheduler::packet>(std::make_shared<pifo_scheduler::packet>());
				dequeue_FB(FB_FS_enq_packet_ptr, deq_packet_ptr->flow_id, in_head_FB);
				deq_packet_ptr = NULL;				
				deq_packet_ptr = FB_FS_enq_packet_ptr;
			}

		}

		if (in_deq == 1)
			out_deq_pkt_ptr = deq_packet_ptr;
		else
			out_deq_pkt_ptr = NULL;
	}

// used by pifo function to enqueue inside a certain flow scheduler
	void enqueue_FS(std::shared_ptr<pifo_scheduler::packet> new_packet_ptr, std::shared_ptr<pifo_scheduler::flow_scheduler>& head_FS)
	{
		std::shared_ptr<pifo_scheduler::flow_scheduler> cur_ptr_FS;
		cur_ptr_FS = std::shared_ptr<pifo_scheduler::flow_scheduler>(std::make_shared<pifo_scheduler::flow_scheduler>());
		std::shared_ptr<pifo_scheduler::flow_scheduler> prev_ptr_FS;
		prev_ptr_FS = std::shared_ptr<pifo_scheduler::flow_scheduler>(std::make_shared<pifo_scheduler::flow_scheduler>());
		if (head_FS == NULL)
		{
			head_FS = std::shared_ptr<pifo_scheduler::flow_scheduler>(std::make_shared<pifo_scheduler::flow_scheduler>());
			head_FS->object = new_packet_ptr;
			head_FS->next = NULL;
			cur_ptr_FS = head_FS;
		}
		else
		{
			cur_ptr_FS = head_FS;
			prev_ptr_FS = NULL;
			std::shared_ptr<pifo_scheduler::flow_scheduler> temp_ptr;
			temp_ptr = std::shared_ptr<pifo_scheduler::flow_scheduler>(std::make_shared<pifo_scheduler::flow_scheduler>());
			temp_ptr->object = new_packet_ptr;
			while ((cur_ptr_FS != NULL))
			{
				if(cur_ptr_FS->object->rank < new_packet_ptr->rank)
				{
				prev_ptr_FS = cur_ptr_FS;
				cur_ptr_FS = cur_ptr_FS->next;
				}
				else
				{
				break;
				}
			}
			temp_ptr->next = cur_ptr_FS;
			if (prev_ptr_FS == NULL)
			{
				head_FS = temp_ptr;
			}
			else
			{
				prev_ptr_FS->next = temp_ptr;
			}
		}
	}

// used by pifo function to enqueue inside a certain FIFO bank
	void enqueue_FB(std::shared_ptr<pifo_scheduler::packet> new_packet_ptr, std::shared_ptr<pifo_scheduler::fifo_bank>& head_FB)
	{
		std::shared_ptr<pifo_scheduler::fifo_bank> cur_ptr_FB;
		cur_ptr_FB = std::shared_ptr<pifo_scheduler::fifo_bank>(std::make_shared<pifo_scheduler::fifo_bank>());
		std::shared_ptr<pifo_scheduler::fifo_bank> prev_ptr_FB;
		prev_ptr_FB = std::shared_ptr<pifo_scheduler::fifo_bank>(std::make_shared<pifo_scheduler::fifo_bank>());
		std::shared_ptr<pifo_scheduler::fifo_bank> temp_ptr;
		temp_ptr = std::shared_ptr<pifo_scheduler::fifo_bank>(std::make_shared<pifo_scheduler::fifo_bank>());
		temp_ptr->object = new_packet_ptr;
		temp_ptr->left = NULL;
		temp_ptr->bottom = NULL;
		if (head_FB == NULL)
		{
			head_FB = std::shared_ptr<pifo_scheduler::fifo_bank>(std::make_shared<pifo_scheduler::fifo_bank>());
			head_FB->flow_id = new_packet_ptr->flow_id;
			head_FB->bottom = NULL;
			head_FB->left = temp_ptr;
			cur_ptr_FB = head_FB;
		}
		else
		{
			cur_ptr_FB = head_FB;
			prev_ptr_FB = NULL;
			unsigned int flow_id_FB_found = 0;
			while (cur_ptr_FB != NULL)
			{
				if (cur_ptr_FB->flow_id == new_packet_ptr->flow_id)
				{
					flow_id_FB_found = 1;
					while (cur_ptr_FB->left != NULL)
					{
						cur_ptr_FB = cur_ptr_FB->left;
					}
					cur_ptr_FB->left = temp_ptr;
					break;
				}
				prev_ptr_FB = cur_ptr_FB;
				cur_ptr_FB = cur_ptr_FB->bottom;
			}
			if (flow_id_FB_found == 0)
			{
				std::shared_ptr<pifo_scheduler::fifo_bank> temp_ptr2;
				temp_ptr2 = std::shared_ptr<pifo_scheduler::fifo_bank>(std::make_shared<pifo_scheduler::fifo_bank>());
				temp_ptr2->flow_id = new_packet_ptr->flow_id;
				temp_ptr2->left = temp_ptr;
				temp_ptr2->bottom = NULL;
				prev_ptr_FB->bottom = temp_ptr2;
			}
		}

	}

	void dequeue_FS(std::shared_ptr<pifo_scheduler::packet>& deq_packet_ptr, std::shared_ptr<pifo_scheduler::flow_scheduler>& head_FS, unsigned int time_now)
	{
		std::shared_ptr<pifo_scheduler::flow_scheduler> cur_ptr_FS;
		cur_ptr_FS = std::shared_ptr<pifo_scheduler::flow_scheduler>(std::make_shared<pifo_scheduler::flow_scheduler>());
		std::shared_ptr<pifo_scheduler::flow_scheduler> prev_ptr_FS;
		prev_ptr_FS = std::shared_ptr<pifo_scheduler::flow_scheduler>(std::make_shared<pifo_scheduler::flow_scheduler>());
		deq_packet_ptr = NULL;
		if (head_FS != NULL)
		{
			cur_ptr_FS = head_FS;
			prev_ptr_FS = NULL;
			if ((cur_ptr_FS->object->pred <= time_now)&& (cur_ptr_FS->object->pkt_ptr <= number_of_read_packets)) // Elbediwy : I add this, to synchronize with the switch multithreading
			{
				deq_packet_ptr = head_FS->object;
				head_FS = head_FS->next;
			}
			else
			{
				while (cur_ptr_FS != NULL)
				{
					if ((cur_ptr_FS->object->pred <= time_now) && (cur_ptr_FS->object->pkt_ptr <= number_of_read_packets)) // Elbediwy : I add this, to synchronize with the switch multithreading
					{
						deq_packet_ptr = cur_ptr_FS->object;
						prev_ptr_FS->next = cur_ptr_FS->next;
						break;
					}
					prev_ptr_FS = cur_ptr_FS;
					cur_ptr_FS = cur_ptr_FS->next;
				}
			}
		}
	}

// used by pifo function to dequeue from a certain FIFO bank
	void dequeue_FB(std::shared_ptr<pifo_scheduler::packet>& deq_packet_ptr, unsigned int flow_id, std::shared_ptr<pifo_scheduler::fifo_bank>& head_FB)
	{
		std::shared_ptr<pifo_scheduler::fifo_bank> cur_ptr_FB;
		cur_ptr_FB = std::shared_ptr<pifo_scheduler::fifo_bank>(std::make_shared<pifo_scheduler::fifo_bank>());
		deq_packet_ptr = NULL;
		cur_ptr_FB = head_FB;
		while (cur_ptr_FB != NULL)
		{
			if ((cur_ptr_FB->flow_id == flow_id) && (cur_ptr_FB->left != NULL))
			{
				deq_packet_ptr = cur_ptr_FB->left->object;
				if (deq_packet_ptr != NULL) {
					cur_ptr_FB->left = cur_ptr_FB->left->left;
				}

				break;
			}
			cur_ptr_FB = cur_ptr_FB->bottom;
		}
	}

// return the last enqueued packet pointer to be used in the buffer inside the "Simple_switch" target
	unsigned int get_last_pkt_ptr()
	{
		if(!pkt_ptr_queue.empty())
		{
			unsigned int current_pkt_ptr = pkt_ptr_queue.front();
			pkt_ptr_queue.pop();
			number_of_read_packets = number_of_read_packets + 1;
			return current_pkt_ptr;			
		}
		else
		{
			return 0;
		}
	}

// Apply dequeue operation in the scheduler, will be used inside the "Simple_Switch" target
	unsigned int dequeue_my_scheduler()
	{
	flow_id = 0;
	pred = 0;
	enq = 0;
	unsigned int null_ptr = 0;
	deq = 1;


	run_core();

	if(deq_packet_ptr != NULL)
	{
		number_of_dequeue_packets = number_of_dequeue_packets + 1;
		
		switch_is_ready = 0;

		if(pkt_ptr !=0)
		pkt_ptr = 0;
		return deq_packet_ptr->pkt_ptr;
	}	
	else
	{
	return null_ptr;
	}
	}

// return the number of enqueued packets until now to the "Simple_Switch".
	unsigned int number_of_enq_pkts()
	{
		return number_of_enqueue_packets;
	}


	unsigned int number_of_deq_pkts()
	{
		return number_of_dequeue_packets;
	}

// return the number of captured packets until now by the TM_buffer.h.
	unsigned int num_of_read_pkts()
	{
		return number_of_read_packets;
	}

// A flag from the target switch, indicating that the switch is ready to receive a new dequeued pkt from the TM
	void start_dequeue(unsigned int start)
	{
		switch_is_ready = start;
	}

// reset the number of enqueued packets to zero by the "Simple_Switch"
	void reset_number_of_enq_pkts()
	{
		number_of_enqueue_packets = 0;
	}


 private:
  // Attribute
  Data verbose{};

  // Data members
  bool verbose_{false};
};

}  // namespace bm
#endif
