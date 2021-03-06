#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <assert.h>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <iomanip>

using namespace std;

class Entry {
    public:
        string _hostname;
        pid_t  _pid;
        string _path;
        string _operation;
        off_t  _offset;
        int    _length;
        double _start_time;
        double _end_time;
        
        void show() const;
};

void Entry::show() const
{
    cout << _hostname << " "
         << _pid << " "
         << _path << " "
         << _operation << " "
         << _offset << " "
         << _length << " "
         << _start_time << " "
         << _end_time << endl;
}

class Replayer{
    public:
        vector<Entry> _trace;
        int _fd;

        // initial these before playing
        int _sleeptime;
        int _customized_sleeptime; // 0 or 1
        int _do_pread; // 0 or 1
        int _do_prefetch; // 0 or 1
        string _trace_path;
        string _data_path;
        int _period;
        int _do_period;

        // Performance returned
        off_t _preadbytes;
        double _readtime;

        void readTrace();
        void play();
        double playTime();
        void prePlay();
        void postPlay();
        void prefetch();

        Replayer();
};

Replayer::Replayer()
    :_preadbytes(0),
     _readtime(0)
{}


void Replayer::prefetch()
{
    assert( !_trace.empty() );

    vector<Entry>::const_iterator cit;

    for ( cit = _trace.begin();
          cit != _trace.end();
          cit++ )
    {
        posix_fadvise( _fd, cit->_offset, cit->_length, POSIX_FADV_WILLNEED);
    }
    
    sleep(20);
}

void Replayer::prePlay()
{
     _fd = open( _data_path.c_str(), O_RDONLY );
    assert( _fd != -1 );
}

void Replayer::postPlay()
{
    close(_fd);
}

void Replayer::readTrace()
{
    FILE *fp;
    

    fp = fopen(_trace_path.c_str(), "r");
    assert(fp != NULL);

    while ( !feof(fp) ) {
        Entry entry;

        char path[256];
        char operation[256];
        char hostname[256];
        int ret = 0;
        ret += fscanf(fp, "%s", hostname); 
        entry._hostname = hostname;

        ret += fscanf(fp, "%u", &entry._pid);

        ret += fscanf(fp, "%s", path);
        entry._path.assign(path);
        
        ret += fscanf(fp, "%s", operation);
        entry._operation.assign(operation);

        if ( entry._operation == "trc_read" ||
             entry._operation == "trc_write" ) 
        {
            ret += fscanf(fp, "%lld", &entry._offset);
            ret += fscanf(fp, "%d", &entry._length);
        } else {
            char eater[128];
            ret += fscanf(fp, "%s", eater);
            ret += fscanf(fp, "%s", eater);
        }

        ret += fscanf(fp, "%lf", &entry._start_time);
        ret += fscanf(fp, "%lf", &entry._end_time);

        if ( entry._operation != "trc_read" ) 
            continue; // skip non-read operations in this version


        if ( ret != 8 ) {
            break;
        }
        
        //entry.show();

        _trace.push_back( entry );
    }
    fclose(fp);
}


// TODO: the trace should have only one filepath column
void Replayer::play()
{
    assert( !_trace.empty() );

    vector<Entry>::const_iterator cit;
    int total = 0;
    int cnt = 0;

    for ( cit = _trace.begin();
          cit != _trace.end();
          cit++, cnt++ )
    {
        void *data = malloc(cit->_length);
        assert( data != NULL );

        if ( cit != _trace.begin() ) {
            // sleep to simulate computation
            //cit->show();
            vector<Entry>::const_iterator precit;
            precit = cit;
            precit--;
            int sleeptime = (cit->_start_time - precit->_end_time) * 1000000;
            assert( sleeptime >= 0 );
            if ( _customized_sleeptime == 1 ) {
                usleep( _sleeptime );
            } else {
                usleep( sleeptime );
            }
        }

        if ( _do_period == 1 && cnt % _period == 0 ) {
            vector<Entry>::const_iterator pcit;
            vector<Entry>::const_iterator start;
            vector<Entry>::const_iterator end;

            start = _trace.begin() + cnt + _period/2;
            end = _trace.begin() + cnt + _period/2 + _period;
            for ( pcit = start ;
                  pcit != end &&  _trace.end() - pcit > 0  ;
                  pcit++ ) 
            {
                off_t startblock, endblock;
                off_t blocksize = 8129;
                startblock = pcit->_offset / blocksize;
                endblock = (pcit->_offset + pcit->_length) / blocksize;
                int i;
                for ( i = startblock ; i <= endblock ; i++ ) {
                    posix_fadvise( _fd, i*blocksize, blocksize, POSIX_FADV_WILLNEED);
                }
            }
        }

        if ( _do_pread == 1 ) {
            int ret = pread(_fd, data, cit->_length, cit->_offset);
            assert(ret != -1);
            total += ret;
        }

        free(data);
    }

    //cout << total << " " ;// "(" << total/1024 << "KB)" << endl;
    _preadbytes = total;
}


double Replayer::playTime()
{
    struct timeval start, end, result;  

    // build trace
    readTrace();

    prePlay();

    if ( _do_prefetch == 1 ) {
        prefetch();
    }

#if 0
    // play trace and time it
    gettimeofday(&start, NULL);
    play();
    gettimeofday(&end, NULL);

    timersub( &end, &start, &result );
    printf("%ld.%.6ld ", result.tv_sec, result.tv_usec);
#endif

    // play trace and time it
    gettimeofday(&start, NULL);
    play();
    gettimeofday(&end, NULL);

    timersub( &end, &start, &result );
    //printf("%ld.%.6ld\n", result.tv_sec, result.tv_usec);


    postPlay();

    _readtime = result.tv_sec + result.tv_usec/1000000.0;
    return _readtime;
}

int main(int argc, char **argv)
{
    if ( argc != 9 ) {
        printf("Usage: %s trace-file data-file sleeptime customized-sleeptime do-pread do-whole-prefetch\
                do-period-prefetch period\n", argv[0]);
        return 1;
    }

    Replayer replayer;

    // init
    replayer._trace_path =  argv[1];
    replayer._data_path = argv[2];
    replayer._sleeptime = atoi(argv[3]);
    replayer._customized_sleeptime = atoi(argv[4]);
    replayer._do_pread = atoi(argv[5]);
    replayer._do_prefetch = atoi(argv[6]);
    replayer._do_period = atoi(argv[7]);
    replayer._period = atoi(argv[8]);

    cout 
        << setw(20) 
        << "Trace.Path" 
        << setw(20) 
        << "Data.Path"
        << setw(15) 
        << "Sleep.Time"
        << setw(25) 
        << "Customize.Sleeptime"
        << setw(10) 
        << "Do.Pread"
        << setw(15) 
        << "Bytes.Pread" 
        << setw(15) 
        << "Read.Time"
        << setw(15)
        << "Do.Prefetch"
        << setw(10)
        << "Do.Period"
        << setw(10)
        << "Period"
        << endl;

    replayer.playTime();

    cout 
        << setw(20) 
        << replayer._trace_path.substr(0, 19)
        << setw(20) 
        << replayer._data_path.substr(0,19)
        << setw(15) 
        << replayer._sleeptime 
        << setw(25) 
        << replayer._customized_sleeptime 
        << setw(10) 
        << replayer._do_pread 
        << setw(15) 
        << replayer._preadbytes 
        << setw(15) 
        << replayer._readtime
        << setw(15)
        << replayer._do_prefetch
        << setw(10)
        << replayer._do_period
        << setw(10)
        << replayer._period
        << setw(15)
        << "GREPMARKER" << endl;

    return 0;    
}

