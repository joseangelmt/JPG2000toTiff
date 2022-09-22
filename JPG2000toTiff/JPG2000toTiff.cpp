#include <iostream>
#include <NCSECWClient.h>
#include <NCSErrors.h>
#include <ranges>
#include <tiffio.h>
#include <vector>

#ifdef _DEBUG
#pragma comment(lib, "NCSEcwd")
#else
#pragma comment(lib, "NCSEcw")
#endif

#define TAMANO_TILE 1024

template<typename TipoTiff, NCSCellType TipoEcw>
int Trabaja(TIFF* tiff, NCSFileView** fileView, NCSFileViewFileInfoEx** fileInfo, size_t imagenes)
{
	auto const samplesPerPixel = fileInfo[0]->nBands;

	uint16_t photommetric;
	switch (samplesPerPixel)
	{
	case 1:
		photommetric = PHOTOMETRIC_MINISBLACK;
		break;
	case 4:
	case 3:
		photommetric = PHOTOMETRIC_RGB;
		break;
	default:
		return 2;
	}

	UINT32 altoTotal{};
	for(auto i=0u; i<imagenes; i++)
		altoTotal += fileInfo[i]->nSizeY;

	auto const tilesHorizontal = (fileInfo[0]->nSizeX + TAMANO_TILE - 1) / TAMANO_TILE;

	TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, fileInfo[0]->nSizeX);
	TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, altoTotal);
	TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(8) * sizeof(TipoTiff));
	TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
	TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, samplesPerPixel);
	TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, photommetric);
	//TIFFSetField(tiff, TIFFTAG_SOFTWARE, L"Digi3D.NET");
	TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tiff, TIFFTAG_TILEWIDTH, TAMANO_TILE);
	TIFFSetField(tiff, TIFFTAG_TILELENGTH, TAMANO_TILE);

	auto** tilesDestino = new TipoTiff * [tilesHorizontal];
	for (auto i = 0u; i < tilesHorizontal; i++) {
		tilesDestino[i] = new TipoTiff[TAMANO_TILE * TAMANO_TILE * samplesPerPixel];
	}

	UINT listaBandas[]{ 0,1,2,3 };

	auto* p_p_output_line = new TipoTiff * [samplesPerPixel];
	for (auto i = 0; i < samplesPerPixel; i++)
		p_p_output_line[i] = new TipoTiff[fileInfo[0]->nSizeX];

	auto oldPorciento = -1;
	auto filaTotal = 0u;
	int filaEnTile = 0;
	for (auto i = 0u; i < imagenes; i++) {
		if (NCS_SUCCESS != NCScbmSetFileView(
			fileView[i],
			fileInfo[i]->nBands,
			listaBandas,
			0,
			0,
			fileInfo[i]->nSizeX - 1,
			fileInfo[i]->nSizeY - 1,
			fileInfo[i]->nSizeX,
			fileInfo[i]->nSizeY))
			return 1;

		for (auto fila = 0u; fila < fileInfo[i]->nSizeY; fila++, filaTotal++) {
			if (auto const porciento = static_cast < int>(100 * filaTotal / altoTotal); porciento != oldPorciento) {
				oldPorciento = porciento;
				std::cout << porciento << "%" << std::endl;
			}
			if (filaTotal + 1 == altoTotal)
				break;

			if(NCS_READ_OK != NCScbmReadViewLineBILEx(fileView[i], TipoEcw, reinterpret_cast<void**>(p_p_output_line)))
				return 1;

			for (auto tileHorizontal = 0u; tileHorizontal < tilesHorizontal; tileHorizontal++) {
				auto* destino = tilesDestino[tileHorizontal] + filaEnTile * TAMANO_TILE * samplesPerPixel;
				auto const numeroColumnas = (tileHorizontal * TAMANO_TILE + TAMANO_TILE > fileInfo[0]->nSizeX ? fileInfo[0]->nSizeX - tileHorizontal * TAMANO_TILE : TAMANO_TILE);

				for (auto columna = 0u; columna < numeroColumnas; columna++) {
					for (auto banda = 0; banda < samplesPerPixel; banda++)
						*destino++ = p_p_output_line[banda][tileHorizontal * TAMANO_TILE + columna];
				}
			}

			filaEnTile++;
			if (filaEnTile == TAMANO_TILE) {
				// Se ha rellenado la tesela. La almacenamos.
				filaEnTile = 0;

				for (auto tileHorizontal = 0u; tileHorizontal < tilesHorizontal; tileHorizontal++) {
					TIFFWriteTile(tiff, static_cast<void*>(tilesDestino[tileHorizontal]), tileHorizontal * TAMANO_TILE, filaTotal, 0, 0);
				}
			}
		}
	}

	for (auto tileHorizontal = 0u; tileHorizontal < tilesHorizontal; tileHorizontal++) {
		TIFFWriteTile(tiff, static_cast<void*>(tilesDestino[tileHorizontal]), tileHorizontal * TAMANO_TILE, filaTotal, 0, 0);
	}

	TIFFClose(tiff);
	return 0;
}

int wmain(int args, wchar_t* argv[])
{
	std::cout << "JPG2000toTiff" << std::endl;

	if( args < 3 )
	{
		std::cerr << "Error: Not enough parameters have been specified." << std::endl;
		std::cerr << "The format is:" << std::endl;
		std::cerr << "JPG2000toTIFF [tiff file to be created] [jp2 file number 1]...[jp2 file number n]" << std::endl;
		std::cerr << "If more than one .jp2 file is specified, a single TIFF will be created in which the different .jp2 files will be concatenated from top to bottom." << std::endl;
		return 1;
	}

	std::wstring archivoCrear{ argv[1] };
	auto const archivosJp2{ args - 2 };

	std::unique_ptr< NCSFileView* []> fileView{ new NCSFileView * [archivosJp2] };
	std::unique_ptr<NCSFileViewFileInfoEx* []> fileInfo{ new NCSFileViewFileInfoEx * [archivosJp2] };

	for(auto i=0; i< archivosJp2; i++)
	{
		if (NCScbmOpenFileView(argv[2 + i], &fileView[i], nullptr)  != NCS_SUCCESS)
			return 1;

		NCScbmGetViewFileInfoEx(fileView[i], &fileInfo[i]);
	}

	switch (fileInfo[0]->nBands)
	{
	case 1:
	case 4:
	case 3:
		break;
	default:
		std::cout << "No se permite cargar imágenes con: " << fileInfo[0]->nBands << " canales" << std::endl;
		return 2;
	}

	auto const tiff = TIFFOpenW(argv[1], "w8");

	switch (fileInfo[0]->eCellType)
	{
	case NCSCT_UINT8:
		return Trabaja<BYTE, NCSCT_UINT8>(tiff, fileView.get(), fileInfo.get(), archivosJp2);
	case NCSCT_UINT16:
		return Trabaja<USHORT, NCSCT_UINT16>(tiff, fileView.get(), fileInfo.get(), archivosJp2);
	default:
		return 2;
	}
}
