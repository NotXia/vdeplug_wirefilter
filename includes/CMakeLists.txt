add_library(wf_conn wf_conn.c)

add_library(wf_time wf_time.c)

add_library(wf_queue wf_queue.c)

add_library(wf_markov wf_markov.c)
target_link_libraries(wf_markov m)

add_library(wf_management wf_management.c)

add_library(wf_log wf_log.c)