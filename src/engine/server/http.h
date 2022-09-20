#ifndef ENGINE_CLIENT_HTTP_H
#define ENGINE_CLIENT_HTTP_H

#include <atomic>
#include <engine/kernel.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

typedef struct _json_value json_value;
typedef void CURL;

enum
{
	HTTP_ERROR = -1,
	HTTP_QUEUED,
	HTTP_RUNNING,
	HTTP_DONE,
	HTTP_ABORTED,
};

enum class HTTPLOG
{
	NONE,
	FAILURE,
	ALL,
};

struct CTimeout
{
	long ConnectTimeoutMs;
	long LowSpeedLimit;
	long LowSpeedTime;
};

class CRequest : public IJob
{
	// Abort the request with an error if `BeforeInit()` or `AfterInit()`
	// returns false. Also abort the request if `OnData()` returns
	// something other than `DataSize`.
	virtual bool BeforeInit() { return true; }
	virtual bool AfterInit(void *pCurl) { return true; }
	virtual size_t OnData(char *pData, size_t DataSize) = 0;

	virtual void OnProgress() {}

	char m_aUrl[256];

	CTimeout m_Timeout;

	std::atomic<double> m_Size;
	std::atomic<double> m_Current;
	std::atomic<int> m_Progress;
	HTTPLOG m_LogProgress;

	std::atomic<int> m_State;
	std::atomic<bool> m_Abort;

	static int ProgressCallback(void *pUser, double DlTotal, double DlCurr, double UlTotal, double UlCurr);
	static size_t WriteCallback(char *pData, size_t Size, size_t Number, void *pUser);

	void Run();
	int RunImpl(CURL *pHandle);

protected:
	virtual int OnCompletion(int State) { return State; }

public:
	CRequest(const char *pUrl, CTimeout Timeout, HTTPLOG LogProgress = HTTPLOG::ALL);

	double Current() const { return m_Current.load(std::memory_order_relaxed); }
	double Size() const { return m_Size.load(std::memory_order_relaxed); }
	int Progress() const { return m_Progress.load(std::memory_order_relaxed); }
	int State() const { return m_State; }
	void Abort() { m_Abort = true; }
};

class CHead : public CRequest
{
	virtual size_t OnData(char *pData, size_t DataSize) { return DataSize; }
	virtual bool AfterInit(void *pCurl);

public:
	CHead(const char *pUrl, CTimeout Timeout, HTTPLOG LogProgress = HTTPLOG::ALL);
	~CHead();
};

class CGet : public CRequest
{
	virtual size_t OnData(char *pData, size_t DataSize);

	size_t m_BufferSize;
	size_t m_BufferLength;
	unsigned char *m_pBuffer;

public:
	CGet(const char *pUrl, CTimeout Timeout, HTTPLOG LogProgress = HTTPLOG::ALL);
	~CGet();

	size_t ResultSize() const
	{
		if(!Result())
		{
			return 0;
		}
		else
		{
			return m_BufferSize;
		}
	}
	unsigned char *Result() const;
	unsigned char *TakeResult();
	json_value *ResultJson() const;
};

class CGetFile : public CRequest
{
	virtual size_t OnData(char *pData, size_t DataSize);
	virtual bool BeforeInit();

	IStorage *m_pStorage;

	char m_aDestFull[IO_MAX_PATH_LENGTH];
	IOHANDLE m_File;

protected:
	char m_aDest[IO_MAX_PATH_LENGTH];
	int m_StorageType;

	virtual int OnCompletion(int State);

public:
	CGetFile(IStorage *pStorage, const char *pUrl, const char *pDest, int StorageType = -2, CTimeout Timeout = CTimeout{4000, 500, 5}, HTTPLOG LogProgress = HTTPLOG::ALL);

	const char *Dest() const { return m_aDest; }
};

class CPostJson : public CRequest
{
	virtual size_t OnData(char *pData, size_t DataSize) { return DataSize; }
	virtual bool AfterInit(void *pCurl);

	char m_aJson[1024];

public:
	CPostJson(const char *pUrl, CTimeout Timeout, const char *pJson);
};

bool HttpInit(IStorage *pStorage);
void EscapeUrl(char *pBuf, int Size, const char *pStr);
#endif // ENGINE_CLIENT_HTTP_H
