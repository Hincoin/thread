//  (C) Copyright 2005-6 Anthony Williams
// Copyright 2006 Roland Schwarz.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
// This work is a reimplementation along the design and ideas
// of William E. Kempf.

#ifndef BOOST_THREAD_RS06041001_HPP
#define BOOST_THREAD_RS06041001_HPP

#include <boost/thread/win32/config.hpp>

#include <boost/detail/interlocked.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/xtime.hpp>
#include <boost/thread/win32/thread_primitives.hpp>
#include <boost/thread/win32/xtime_utils.hpp>
#include <boost/thread/win32/interlocked_read.hpp>

namespace boost
{
    class condition
    {
    private:
        struct waiting_list_entry
        {
            void* waiting_thread_handle;
            waiting_list_entry* next;
            waiting_list_entry* previous;
            long notified;

            void unlink()
            {
                next->previous=previous;
                previous->next=next;
                next=this;
                previous=this;
            }
        };

        typedef ::boost::mutex gate_type;
        gate_type state_change_gate;
        typedef gate_type::scoped_lock gate_scoped_lock;
        waiting_list_entry waiting_list;

        template<typename scoped_lock_type>
        struct add_entry_to_list
        {
            condition* self;
            waiting_list_entry& entry;
            scoped_lock_type& m;

            add_entry_to_list(condition* self_,waiting_list_entry& entry_,scoped_lock_type& m_):
                self(self_),entry(entry_),m(m_)
            {
                gate_scoped_lock lock(self->state_change_gate);
                    
                entry.next=self->waiting_list.next;
                self->waiting_list.next=&entry;
                entry.next->previous=&entry;
                    
                m.unlock();
            }
            ~add_entry_to_list()
            {
                void* thread_handle;
                        
                {
                    gate_scoped_lock lock(self->state_change_gate);
                        
                    thread_handle=entry.waiting_thread_handle;
                    entry.unlink();
                    entry.waiting_thread_handle=0;
                }
                m.lock();
                BOOST_CLOSE_HANDLE(thread_handle);
            }
        };
        

        template<typename scoped_lock_type>
        bool do_wait(scoped_lock_type& m,boost::xtime const& target=::boost::detail::get_xtime_sentinel())
        {
            waiting_list_entry entry={0};
            void* const currentProcess=BOOST_GET_CURRENT_PROCESS();
            
            long const same_access_flag=2;
            BOOST_DUPLICATE_HANDLE(currentProcess,BOOST_GET_CURRENT_THREAD(),currentProcess,&entry.waiting_thread_handle,0,false,same_access_flag);
            
            entry.previous=&waiting_list;
            
            {
                add_entry_to_list<scoped_lock_type> list_guard(this,entry,m);

                unsigned const woken_due_to_apc=0xc0;
                while(!::boost::detail::interlocked_read(&entry.notified) && 
                      BOOST_SLEEP_EX(::boost::detail::get_milliseconds_until_time(target),true)==woken_due_to_apc);
            }
            
            return ::boost::detail::interlocked_read(&entry.notified);
        }

        static void __stdcall notify_function(::boost::detail::ulong_ptr)
        {
        }

        void notify_entry(waiting_list_entry * entry)
        {
            BOOST_INTERLOCKED_EXCHANGE(&entry->notified,true);
            entry->unlink();
            if(entry->waiting_thread_handle)
            {
                BOOST_QUEUE_USER_APC(notify_function,entry->waiting_thread_handle,0);
            }
        }

        bool do_notify_one(waiting_list_entry& list)
        {
            gate_scoped_lock lock(state_change_gate);
            if(list.previous==&list)
            {
                return false;
            }
            notify_entry(list.previous);
            return true;
        }

    public:
        condition()
        {
            waiting_list.next=&waiting_list;
            waiting_list.previous=&waiting_list;
        }

        void notify_one()
        {
            do_notify_one(waiting_list);
        }
        
        void notify_all()
        {
            waiting_list_entry new_list={0};
            {
                gate_scoped_lock lock(state_change_gate);
                new_list.previous=waiting_list.previous;
                new_list.next=waiting_list.next;
                new_list.next->previous=&new_list;
                new_list.previous->next=&new_list;
                waiting_list.previous=&waiting_list;
                waiting_list.next=&waiting_list;
            }

            while(do_notify_one(new_list));
        }

        template<typename scoped_lock_type>
        void wait(scoped_lock_type& m)
        {
            do_wait(m);
        }

        template<typename scoped_lock_type,typename predicate_type>
        void wait(scoped_lock_type& m,predicate_type pred)
        {
            while(!pred()) do_wait(m);
        }

        template<typename scoped_lock_type>
        bool timed_wait(scoped_lock_type& m,const xtime& xt)
        {
            return do_wait(m,xt);
        }

        template<typename scoped_lock_type,typename predicate_type>
        bool timed_wait(scoped_lock_type& m,const xtime& xt,predicate_type pred)
        {
            while (!pred()) 
            {
                if (!timed_wait(m, xt)) return false;
            }
            return true;
        }
    };

}

#endif // BOOST_THREAD_RS06041001_HPP