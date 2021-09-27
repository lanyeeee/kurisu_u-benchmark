#include <algorithm>
#include <iostream>
#include <list>
#include <string>
#include <mutex>
#include <assert.h>
#include <vector>
#include <memory>
#include <queue>
#include <stack>
#include <thread>
#define ASIO_STANDALONE
#define ASIO_HAS_STD_CHRONO
#include <asio.hpp>
#include <vector>
#include <memory>


struct noncopyable {
protected:
    noncopyable() {}
    virtual ~noncopyable() {}

private:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;
    noncopyable(noncopyable&&) = delete;
    noncopyable& operator=(noncopyable&&) = delete;
};



/// A pool of io_service objects.
class io_service_pool : noncopyable {
public:
    /// Construct the io_service pool.
    explicit io_service_pool(std::size_t pool_size);

    /// Run all io_service objects in the pool.
    void run();

    /// Stop all io_service objects in the pool.
    void stop();

    /// Get an io_service to use.
    asio::io_service& get_io_service();

    asio::io_service& get_io_service(size_t index);

private:
    typedef std::shared_ptr<asio::io_service> io_service_ptr;
    typedef std::shared_ptr<asio::io_service::work> work_ptr;

    /// The pool of io_services.
    std::vector<io_service_ptr> io_services_;

    /// The work that keeps the io_services running.
    std::vector<work_ptr> work_;

    /// The next io_service to use for a connection.
    std::size_t next_io_service_;
};


io_service_pool::io_service_pool(std::size_t pool_size)
    : next_io_service_(0)
{
    if (pool_size == 0)
        throw std::runtime_error("io_service_pool size is 0");

    // Give all the io_services work to do so that their run() functions will not
    // exit until they are explicitly stopped.
    for (std::size_t i = 0; i < pool_size; ++i)
    {
        io_service_ptr io_service(new asio::io_service);
        work_ptr work(new asio::io_service::work(*io_service));
        io_services_.push_back(io_service);
        work_.push_back(work);
    }
}

void io_service_pool::run()
{
    // Create a pool of threads to run all of the io_services.
    std::vector<std::shared_ptr<std::thread>> threads;
    for (std::size_t i = 0; i < io_services_.size(); ++i)
    {
        std::shared_ptr<std::thread> thread(new std::thread(
            [i, this]() {
                io_services_[i]->run();
            }));
        threads.push_back(thread);
    }

    // Wait for all threads in the pool to exit.
    for (std::size_t i = 0; i < threads.size(); ++i)
        threads[i]->join();
}

void io_service_pool::stop()
{
    // Explicitly stop all io_services.
    for (std::size_t i = 0; i < work_.size(); ++i)
        work_[i].reset();
}

asio::io_service& io_service_pool::get_io_service()
{
    // Use a round-robin scheme to choose the next io_service to use.
    asio::io_service& io_service = *io_services_[next_io_service_];
    ++next_io_service_;
    if (next_io_service_ == io_services_.size())
        next_io_service_ = 0;
    return io_service;
}

asio::io_service& io_service_pool::get_io_service(size_t index)
{
    index = index % io_services_.size();
    return *io_services_[index];
}


namespace {

    const size_t kMaxStackSize = 10;

    class session : public std::enable_shared_from_this<session>, noncopyable {
    public:
        session(asio::io_service& ios, size_t block_size)
            : io_service_(ios), socket_(ios), block_size_(block_size)
        {
            for (size_t i = 0; i < kMaxStackSize; ++i)
            {
                buffers_.push(new char[block_size_]);
            }
        }

        ~session()
        {
            while (!buffers_.empty())
            {
                auto buffer = buffers_.top();
                delete[] buffer;
                buffers_.pop();
            }
        }

        asio::ip::tcp::socket& socket()
        {
            return socket_;
        }

        void start()
        {
            asio::ip::tcp::no_delay no_delay(true);
            socket_.set_option(no_delay);
            read();
        }

        void write(char* buffer, size_t len)
        {
            socket_.async_write_some(asio::buffer(buffer, len),
                                     [this, self = shared_from_this(), buffer](
                                         const asio::error_code& err, size_t cb) {
                                         bool need_read = buffers_.empty();
                                         buffers_.push(buffer);
                                         if (need_read && !err)
                                         {
                                             read();
                                         }
                                         if (err)
                                         {
                                             close();
                                         }
                                     });
        }

        void read()
        {
            if (buffers_.empty())
            {
                std::cout << "the cached buffer used out\n";
                return;
            }

            auto buffer = buffers_.top();
            buffers_.pop();

            socket_.async_read_some(asio::buffer(buffer, block_size_),
                                    [this, self = shared_from_this(), buffer](
                                        const asio::error_code& err, size_t cb) {
                                        if (!err)
                                        {
                                            write(buffer, cb);
                                            read();
                                        }
                                        else
                                        {
                                            close();
                                        }
                                    });
        }

    private:
        void close()
        {
            socket_.close();
        }

    private:
        asio::io_service& io_service_;
        asio::ip::tcp::socket socket_;
        size_t const block_size_;
        std::stack<char*> buffers_;
    };

    class server : noncopyable {
    public:
        server(int thread_count, const asio::ip::tcp::endpoint& endpoint, size_t block_size)
            : thread_count_(thread_count), block_size_(block_size), service_pool_(thread_count), acceptor_(service_pool_.get_io_service())
        {
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(1));
            acceptor_.bind(endpoint);
            acceptor_.listen();
        }

        void start()
        {
            accept();
        }

        void wait()
        {
            service_pool_.run();
        }

    private:
        void accept()
        {
            std::shared_ptr<session> new_session(new session(
                service_pool_.get_io_service(), block_size_));
            auto& socket = new_session->socket();
            acceptor_.async_accept(socket,
                                   [this, new_session = std::move(new_session)](
                                       const asio::error_code& err) {
                                       if (!err)
                                       {
                                           new_session->start();
                                           accept();
                                       }
                                   });
        }

    private:
        int const thread_count_;
        size_t const block_size_;
        io_service_pool service_pool_;
        asio::ip::tcp::acceptor acceptor_;
    };

}  // namespace

int main()
{
    auto endpoint = asio::ip::tcp::endpoint(
        asio::ip::address::from_string("127.0.0.1"), 5005);
    server s(4, endpoint, 65535);
    s.start();
    s.wait();
}
