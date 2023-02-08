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

unsigned FilaImagenTotalANumeroImagenVertical(unsigned filaImagenTotal, NCSFileViewFileInfoEx** fileInfo, unsigned columnasImagenes, unsigned filasImagenes)
{
	auto filaImagen = 0u;
	auto filaLectura = 0u;
	auto acumulado = 0u;
	for (auto i = 0u; i < filasImagenes; i++)
	{
		auto const altoEstaImagen = fileInfo[static_cast<size_t>(i * columnasImagenes)]->nSizeY;
		if (filaImagenTotal < acumulado + altoEstaImagen)
		{
			return i;
		}

		acumulado += altoEstaImagen;
	}

	return {};
}

unsigned FilaImagenTotalAFilaLectura(unsigned filaImagenTotal, NCSFileViewFileInfoEx** fileInfo, unsigned columnasImagenes, unsigned filasImagenes)
{
	auto filaImagen = 0u;
	auto filaLectura = 0u;
	auto acumulado = 0u;
	for (auto i = 0u; i < filasImagenes; i++)
	{
		auto const altoEstaImagen = fileInfo[static_cast<size_t>(i * columnasImagenes)]->nSizeY;
		if (filaImagenTotal < acumulado + altoEstaImagen)
		{
			return filaImagenTotal - acumulado;
		}

		acumulado += altoEstaImagen;
	}

	return {};
}

unsigned ColumnaImagenTotalANumeroImagenHorizontal(unsigned columnaImagenTotal, NCSFileViewFileInfoEx** fileInfo, unsigned columnasImagenes, unsigned filasImagenes)
{
	auto filaImagen = 0u;
	auto filaLectura = 0u;
	auto acumulado = 0u;
	for (auto i = 0u; i < columnasImagenes; i++)
	{
		auto const anchoEstaImagen = fileInfo[static_cast<size_t>(i * columnasImagenes)]->nSizeX;
		if (columnaImagenTotal < acumulado + anchoEstaImagen)
		{
			return i;
		}

		acumulado += anchoEstaImagen;
	}

	return {};
}

unsigned ColumnaImagenTotalAColumnaLectura(unsigned columnaImagenTotal, NCSFileViewFileInfoEx** fileInfo, unsigned columnasImagenes, unsigned filasImagenes)
{
	auto filaImagen = 0u;
	auto filaLectura = 0u;
	auto acumulado = 0u;
	for (auto i = 0u; i < columnasImagenes; i++)
	{
		auto const anchoEstaImagen = fileInfo[static_cast<size_t>(i * columnasImagenes)]->nSizeX;
		if (columnaImagenTotal < acumulado + anchoEstaImagen)
		{
			return columnaImagenTotal - acumulado;
		}

		acumulado += anchoEstaImagen;
	}

	return {};
}

template<typename T>
auto IndiceImagenHorizontalAColumnaImagenTotal(T imagenHorizontal, NCSFileViewFileInfoEx** fileInfo)
{
	auto acumulado = 0;
	for (T i = {}; i < imagenHorizontal; ++i)
		acumulado += fileInfo[i]->nSizeX;

	return acumulado;
}


template<typename TipoTiff, NCSCellType TipoEcw>
int Trabaja(TIFF* tiff, NCSFileView** fileView, NCSFileViewFileInfoEx** fileInfo, size_t imagenes, int columnasImagenes, int filasImagenes)
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

	UINT32 anchoTotal{};
	UINT32 altoTotal{};
	unsigned indiceImagen = 0;
	for(auto filaImagen=0; filaImagen<filasImagenes; filaImagen++)
	{
		altoTotal += fileInfo[indiceImagen]->nSizeY;

		for (auto columnaImagen = 0; columnaImagen < columnasImagenes; columnaImagen++, indiceImagen++)
		{
			if (filaImagen == 0) 
			{
				anchoTotal += fileInfo[indiceImagen]->nSizeX;
			}
		}
	}

	auto const tilesHorizontal = (anchoTotal + TAMANO_TILE - 1) / TAMANO_TILE;

	TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, anchoTotal);
	TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, altoTotal);
	TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(8) * sizeof(TipoTiff));
	TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
	TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, samplesPerPixel);
	TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, photommetric);
	TIFFSetField(tiff, TIFFTAG_SOFTWARE, L"Digi3D.NET");
	TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tiff, TIFFTAG_TILEWIDTH, TAMANO_TILE);
	TIFFSetField(tiff, TIFFTAG_TILELENGTH, TAMANO_TILE);

	auto destinoTemp = std::unique_ptr<TipoTiff[]>{ new TipoTiff[tilesHorizontal * TAMANO_TILE * TAMANO_TILE * samplesPerPixel] };
	auto tileDestino = std::unique_ptr<TipoTiff[]>{ new TipoTiff[TAMANO_TILE * TAMANO_TILE * samplesPerPixel] };

	UINT listaBandas[]{ 0,1,2,3 };

	auto* p_p_output_line = new TipoTiff * [samplesPerPixel];
	for (auto i = 0; i < samplesPerPixel; i++)
		p_p_output_line[i] = new TipoTiff[anchoTotal];

	auto oldPorciento = -1;
	int filaEnTile = 0;

	auto filaDentroDeTeselasDeEscritura = 0;
	for(auto filaTotal = 0u; filaTotal < altoTotal;)
	{
		// Averiguamos la fila de imágenes que va a proporcionar información para esta fila
		auto const imagenVertical = FilaImagenTotalANumeroImagenVertical(filaTotal, fileInfo, columnasImagenes, filasImagenes);
		auto filaLectura = FilaImagenTotalAFilaLectura(filaTotal, fileInfo, columnasImagenes, filasImagenes);

		for(auto imagenHorizontal = 0; imagenHorizontal < columnasImagenes; imagenHorizontal++ )
		{
			auto const indiceImagenLectura = imagenVertical * columnasImagenes + imagenHorizontal;

			if (NCS_SUCCESS != NCScbmSetFileView(
				fileView[indiceImagenLectura],
				fileInfo[indiceImagenLectura]->nBands,
				listaBandas,
				0,
				filaLectura,
				fileInfo[indiceImagenLectura]->nSizeX - 1,
				filaLectura + min(fileInfo[indiceImagenLectura]->nSizeY - filaLectura, TAMANO_TILE) - 1,
				fileInfo[indiceImagenLectura]->nSizeX,
				min(fileInfo[indiceImagenLectura]->nSizeY - filaLectura, TAMANO_TILE)))
			{
				return 1;
			}

			auto const columnaDestinoInicialEstaImagenLectura = IndiceImagenHorizontalAColumnaImagenTotal(imagenHorizontal, fileInfo);

			auto copiaFilaDentroDeTeselasDeEscritura = filaDentroDeTeselasDeEscritura;
			auto copiaFilaLecturaDeImagenesOriginales = filaLectura;
			// Recorremos las filas restantes de la tesela a rellenar
			for(; copiaFilaDentroDeTeselasDeEscritura < TAMANO_TILE && copiaFilaLecturaDeImagenesOriginales < fileInfo[indiceImagenLectura]->nSizeY; copiaFilaDentroDeTeselasDeEscritura++, copiaFilaLecturaDeImagenesOriginales++)
			{
				if (NCS_READ_OK != NCScbmReadViewLineBILEx(fileView[indiceImagenLectura], TipoEcw, reinterpret_cast<void**>(p_p_output_line)))
					return 1;

				auto* destino = destinoTemp + (copiaFilaDentroDeTeselasDeEscritura * tilesHorizontal * TAMANO_TILE * samplesPerPixel) + (columnaDestinoInicialEstaImagenLectura * samplesPerPixel);
				for (auto columna = 0u; columna < fileInfo[indiceImagenLectura]->nSizeX; columna++) {
					for (auto banda = 0; banda < samplesPerPixel; banda++)
						*destino++ = p_p_output_line[banda][columna];
				}
			}

			if(imagenHorizontal == columnasImagenes - 1)
			{
				filaDentroDeTeselasDeEscritura = copiaFilaDentroDeTeselasDeEscritura;

				if(filaDentroDeTeselasDeEscritura == TAMANO_TILE || copiaFilaDentroDeTeselasDeEscritura >= altoTotal)
				{
					// Hemos rellenado el buffer temporal, de manera que tenemos que almacenar
					for (auto tileHorizontal = 0u; tileHorizontal < tilesHorizontal; tileHorizontal++) {
						for (auto i = 0; i < TAMANO_TILE; i++)
							memcpy(tileDestino + i * TAMANO_TILE * samplesPerPixel, 
								destinoTemp + i * tilesHorizontal * TAMANO_TILE * samplesPerPixel + tileHorizontal * TAMANO_TILE * samplesPerPixel,
								TAMANO_TILE * samplesPerPixel);

						TIFFWriteTile(tiff, static_cast<void*>(tileDestino), tileHorizontal * TAMANO_TILE, filaTotal, 0, 0);
					}
					filaDentroDeTeselasDeEscritura = 0;
				}

				filaLectura = copiaFilaLecturaDeImagenesOriginales;
				filaTotal += copiaFilaDentroDeTeselasDeEscritura;

				if(auto const porciento = filaTotal * 100 / altoTotal; porciento != oldPorciento )
				{
					std::cout << porciento << "%" << std::endl;
					oldPorciento = porciento;
				}

			}
		}
	}

	TIFFClose(tiff);
	return 0;
}

int wmain(int args, wchar_t* argv[])
{
	std::cout << "JPG2000toTiff" << std::endl;

	if( args < 5 )
	{
		std::cerr << "Error: Not enough parameters have been specified." << std::endl;
		std::cerr << "The format is:" << std::endl;
		std::cerr << "JPG2000toTIFF [tiff file to be created] [num of cols] [num of rows] [jp2 file number 1]...[jp2 file number n] " << std::endl;
		std::cerr << "If more than one .jp2 file is specified, a single TIFF will be created in which the different .jp2 files will be concatenated from top to bottom." << std::endl;
		return 1;
	}

	std::wstring archivoCrear{ argv[1] };

	auto const imagenesHorizontal = _wtoi(argv[2]);
	auto const imagenesVertical = _wtoi(argv[3]);

	if( imagenesHorizontal * imagenesVertical != args - 4)
	{
		std::wcerr << L"No has pasado por parámetros exactamente " << imagenesHorizontal * imagenesVertical << " imágenes" << std::endl;
		return 1;
	}

	auto const numeroImagenes = imagenesHorizontal * imagenesVertical;

	std::unique_ptr< NCSFileView* []> fileView{ new NCSFileView * [numeroImagenes] };
	std::unique_ptr<NCSFileViewFileInfoEx* []> fileInfo{ new NCSFileViewFileInfoEx * [numeroImagenes] };

	for(auto i=0; i< numeroImagenes; i++)
	{
		if (NCScbmOpenFileView(argv[4 + i], &fileView[i], nullptr) != NCS_SUCCESS) {
			std::wcerr << L"Error al abrir el archivo: " << argv[4 + i] << std::endl;
			return 1;
		}

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
		return Trabaja<BYTE, NCSCT_UINT8>(tiff, fileView.get(), fileInfo.get(), numeroImagenes, imagenesHorizontal, imagenesVertical);
	case NCSCT_UINT16:
		return Trabaja<USHORT, NCSCT_UINT16>(tiff, fileView.get(), fileInfo.get(), numeroImagenes, imagenesHorizontal, imagenesVertical);
	default:
		return 2;
	}
}
