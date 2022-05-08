/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/system.h>

#include <engine/console.h>
#include <engine/server/mapconverter.h>
#include <engine/shared/datafile.h>
#include <engine/storage.h>

int main(int argc, const char **argv)
{
	cmdline_fix(&argc, &argv);
	dbg_logger_stdout();

	if(argc < 2 || argc > 3)
	{
		dbg_msg("map_convert_for_client", "Invalid arguments");
		dbg_msg("map_convert_for_client", "Usage: map_convert_for_client <source map filepath> [<dest map filepath>]");
		return -1;
	}

	IStorage *pStorage = CreateLocalStorage();
	IEngineMap *pMap = CreateEngineMap();
	IConsole *pConsole = CreateConsole(0);

	if(!pStorage)
	{
		dbg_msg("map_convert_for_client", "error loading storage");
		return -1;
	}

	IKernel *pKernel = IKernel::Create();
	pKernel->RegisterInterface(pStorage);
	pKernel->RegisterInterface(static_cast<IEngineMap*>(pMap)); // register as both
	pKernel->RegisterInterface(static_cast<IMap*>(pMap));

	const char *pSourceFileName = argv[1];

	if(!pMap->Load(pSourceFileName))
	{
		dbg_msg("map_convert_for_client", "unable to load the source (map) file");
		return -1;
	}

	unsigned ServerMapCrc = 0;
	{
		CDataFileReader dfServerMap;
		dfServerMap.Open(pStorage, pSourceFileName, IStorage::TYPE_ALL);
		ServerMapCrc = dfServerMap.Crc();
		dfServerMap.Close();
	}

	const char *pDestFileName;
	char aDestFileName[IO_MAX_PATH_LENGTH];

	if(argc == 3)
	{
		pDestFileName = argv[2];
	}
	else
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		IStorage::StripPathAndExtension(pSourceFileName, aBuf, sizeof(aBuf));
		str_format(aDestFileName, sizeof(aDestFileName), "clientmaps/%s_%08x.map", aBuf, ServerMapCrc);
		pDestFileName = aDestFileName;
		if(fs_makedir("clientmaps") != 0)
		{
			dbg_msg("map_convert_for_client", "failed to create clientmaps directory");
			return -1;
		}
	}

	CMapConverter MapConverter(pStorage, pMap, pConsole);
	if(!MapConverter.Load())
		return -2;

	if(!MapConverter.CreateMap(pDestFileName))
		return -3;

	delete pKernel;
	delete pConsole;
	delete pMap;
	delete pStorage;

	cmdline_free(argc, argv);
	return 0;
}
