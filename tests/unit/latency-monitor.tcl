start_server {tags {"latency-monitor"}} {
    # Set a threshold high enough to avoid spurious latency events.
    r config set latency-monitor-threshold 200
    r latency reset

    # This parameter is not available in Pika
    test {Test latency events logging} {
        r debug sleep 0.3
        after 1100
        r debug sleep 0.4
        after 1100
        r debug sleep 0.5
        assert {[r latency history command] >= 3}
    }

    # This parameter is not available in Pika
    test {LATENCY HISTORY output is ok} {
        set min 250
        set max 450
        foreach event [r latency history command] {
            lassign $event time latency
            assert {$latency >= $min && $latency <= $max}
            incr min 100
            incr max 100
            set last_time $time ; # Used in the next test
        }
    }

    # This parameter is not available in Pika
    test {LATENCY LATEST output is ok} {
        foreach event [r latency latest] {
            lassign $event eventname time latency max
            assert {$eventname eq "command"}
            assert {$max >= 450 & $max <= 650}
            assert {$time == $last_time}
            break
        }
    }

    # This parameter is not available in Pika
    test {LATENCY HISTORY / RESET with wrong event name is fine} {
        assert {[llength [r latency history blabla]] == 0}
        assert {[r latency reset blabla] == 0}
    }

    # This parameter is not available in Pika
    test {LATENCY DOCTOR produces some output} {
        assert {[string length [r latency doctor]] > 0}
    }

    # This parameter is not available in Pika
    test {LATENCY RESET is able to reset events} {
        assert {[r latency reset] > 0}
        assert {[r latency latest] eq {}}
    }
}
