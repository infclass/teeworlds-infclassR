#include <engine/server/server.h>
#include <engine/server/server_logger.h>

#include <engine/shared/assertion_logger.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/version.h>

#include <teeuniverses/components/localization.h>

#if defined(CONF_FAMILY_WINDOWS)
#define _WIN32_WINNT 0x0501
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <csignal>

int main(int argc, const char **argv) // ignore_convention
{
	CCmdlineFix CmdlineFix(&argc, &argv);
	bool Silent = false;

	for(int i = 1; i < argc; i++)
	{
		if(str_comp("-s", argv[i]) == 0 || str_comp("--silent", argv[i]) == 0)
		{
			Silent = true;
#if defined(CONF_FAMILY_WINDOWS)
			ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
			break;
		}
	}

#if defined(CONF_FAMILY_WINDOWS)
	CWindowsComLifecycle WindowsComLifecycle(false);
#endif

	std::vector<std::shared_ptr<ILogger>> vpLoggers;
#if defined(CONF_PLATFORM_ANDROID)
	vpLoggers.push_back(std::shared_ptr<ILogger>(log_logger_android()));
#else
	if(!Silent)
	{
		vpLoggers.push_back(std::shared_ptr<ILogger>(log_logger_stdout()));
	}
#endif
	std::shared_ptr<CFutureLogger> pFutureFileLogger = std::make_shared<CFutureLogger>();
	vpLoggers.push_back(pFutureFileLogger);
	std::shared_ptr<CFutureLogger> pFutureConsoleLogger = std::make_shared<CFutureLogger>();
	vpLoggers.push_back(pFutureConsoleLogger);
	std::shared_ptr<CFutureLogger> pFutureAssertionLogger = std::make_shared<CFutureLogger>();
	vpLoggers.push_back(pFutureAssertionLogger);
	log_set_global_logger(log_logger_collection(std::move(vpLoggers)).release());

	if(secure_random_init() != 0)
	{
		dbg_msg("secure", "could not initialize secure RNG");
		return -1;
	}

#if !defined(CONF_FAMILY_WINDOWS)
	// As a multithreaded application we have to tell curl to not install signal
	// handlers and instead ignore SIGPIPE from OpenSSL ourselves.
	signal(SIGPIPE, SIG_IGN);
#endif

	CServer *pServer = CreateServer();
	IKernel *pKernel = IKernel::Create();

	// create the components
	IEngine *pEngine = CreateEngine(GAME_NAME, pFutureConsoleLogger, 2);
	IEngineMap *pEngineMap = CreateEngineMap();
	IGameServer *pGameServer = CreateGameServer();
	IConsole *pConsole = CreateConsole(CFGFLAG_SERVER|CFGFLAG_ECON);
	IEngineMasterServer *pEngineMasterServer = CreateEngineMasterServer();
	IStorage *pStorage = CreateStorage(IStorage::STORAGETYPE_SERVER, argc, argv); // ignore_convention
	IConfigManager *pConfigManager = CreateConfigManager();

	pServer->m_pLocalization = new CLocalization(pStorage);
	pServer->m_pLocalization->InitConfig(0, NULL);
	if(!pServer->m_pLocalization->Init())
	{
		dbg_msg("localization", "could not initialize localization");
		return -1;
	}

	pServer->InitRegister(&pServer->m_NetServer, pEngineMasterServer, pConsole);

	{
		bool RegisterFail = false;

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pServer); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngine);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngineMap); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMap *>(pEngineMap), false);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pGameServer);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConsole);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pStorage);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConfigManager);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngineMasterServer); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMasterServer *>(pEngineMasterServer), false);

		if(RegisterFail)
		{
			delete pKernel;
			return -1;
		}
	}

	pEngine->Init();
	pConfigManager->Init();
	pConsole->Init();
	pEngineMasterServer->Init();
	pEngineMasterServer->Load();

	// register all console commands
	pServer->RegisterCommands();

	// execute autoexec file
	pConsole->ExecuteFile("autoexec.cfg");

	// parse the command line arguments
	if(argc > 1)
		pConsole->ParseArguments(argc - 1, &argv[1]);

	log_set_loglevel((LEVEL)g_Config.m_Loglevel);
	if(g_Config.m_Logfile[0])
	{
		IOHANDLE Logfile = pStorage->OpenFile(g_Config.m_Logfile, IOFLAG_WRITE, IStorage::TYPE_SAVE_OR_ABSOLUTE);
		if(Logfile)
		{
			pFutureFileLogger->Set(log_logger_file(Logfile));
		}
		else
		{
			dbg_msg("client", "failed to open '%s' for logging", g_Config.m_Logfile);
		}
	}
	pEngine->SetAdditionalLogger(std::make_unique<CServerLogger>(pServer));

	// run the server
	dbg_msg("server", "starting...");
	int Ret = pServer->Run();

	delete pServer->m_pLocalization;

	// free
	delete pKernel;

	return Ret;
}

// DDRace
