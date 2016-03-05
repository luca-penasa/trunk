//##########################################################################
//#                                                                        #
//#                            CLOUDCOMPARE                                #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 of the License.               #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

//Always first
#include "ccIncludeGL.h"

#include "ccOctree.h"

//Local
#include "ccCameraSensor.h"
#include "ccNormalVectors.h"
#include "ccBox.h"

//CCLib
#include <ScalarFieldTools.h>
#include <RayAndBox.h>

ccOctreeSpinBox::ccOctreeSpinBox(QWidget* parent/*=0*/)
	: QSpinBox(parent)
	, m_octreeBoxWidth(0)
{
	setRange(0,CCLib::DgmOctree::MAX_OCTREE_LEVEL);
	//we'll catch any modification of the spinbox value and update the suffix consequently
	connect(this, SIGNAL(valueChanged(int)), this, SLOT(onValueChange(int)));
}

void ccOctreeSpinBox::setCloud(ccGenericPointCloud* cloud)
{
	if (!cloud)
		return;

	if (cloud->getOctree())
	{
		setOctree(cloud->getOctree());
	}
	else
	{
		ccBBox box = cloud->getOwnBB(false);
		CCLib::CCMiscTools::MakeMinAndMaxCubical(box.minCorner(),box.maxCorner());
		m_octreeBoxWidth = box.getMaxBoxDim();
		onValueChange(value());
	}
}

void ccOctreeSpinBox::setOctree(CCLib::DgmOctree* octree)
{
	if (octree)
	{
		m_octreeBoxWidth = static_cast<double>(octree->getCellSize(0));
		onValueChange(value());
	}
	else
	{
		m_octreeBoxWidth = 0;
		setSuffix(QString());
	}
}

void ccOctreeSpinBox::onValueChange(int level)
{
	if (m_octreeBoxWidth > 0)
	{
		if (level >= 0/* && level <= CCLib::DgmOctree::MAX_OCTREE_LEVEL*/)
		{
			double cs = m_octreeBoxWidth / pow(2.0,static_cast<double>(level));
			setSuffix(QString(" (grid step = %1)").arg(cs));
		}
		else
		{
			//invalid level?!
			setSuffix(QString());
		}
	}
}

ccOctree::ccOctree(ccGenericPointCloud* aCloud)
	: CCLib::DgmOctree(aCloud)
	, ccHObject("Octree")
	, m_theAssociatedCloudAsGPC(aCloud)
	, m_displayType(DEFAULT_OCTREE_DISPLAY_TYPE)
	, m_displayedLevel(1)
	, m_glListID(-1)
	, m_shouldBeRefreshed(true)
	, m_frustrumIntersector(0)
{
	setVisible(false);
	lockVisibility(false);
}

ccOctree::~ccOctree()
{
	if (m_frustrumIntersector)
	{
		delete m_frustrumIntersector;
		m_frustrumIntersector = 0;
	}
}

void ccOctree::setDisplayedLevel(int level)
{
	if (level != m_displayedLevel)
	{
		m_displayedLevel = level;
		m_shouldBeRefreshed = true;
	}
}

void ccOctree::setDisplayType(CC_OCTREE_DISPLAY_TYPE type)
{
	if (m_displayType != type)
	{
		m_displayType = type;
		m_shouldBeRefreshed = true;
	}
}

void ccOctree::clear()
{
	if (m_glListID >= 0)
	{
		if (glIsList(m_glListID))
			glDeleteLists(m_glListID,1);
		m_glListID = -1;
	}

	DgmOctree::clear();
}

ccBBox ccOctree::getOwnBB(bool withGLFeatures/*=false*/)
{
	if (withGLFeatures)
	{
		return ccBBox(m_dimMin,m_dimMax);
	}
	else
	{
		return ccBBox(m_pointsMin,m_pointsMax);
	}
}

void ccOctree::multiplyBoundingBox(const PointCoordinateType multFactor)
{
	m_dimMin *= multFactor;
	m_dimMax *= multFactor;
	m_pointsMin *= multFactor;
	m_pointsMax *= multFactor;

	for (int i=0; i<=MAX_OCTREE_LEVEL; ++i)
		m_cellSize[i] *= multFactor;
}

void ccOctree::translateBoundingBox(const CCVector3& T)
{
	m_dimMin += T;
	m_dimMax += T;
	m_pointsMin += T;
	m_pointsMax += T;
}

void ccOctree::drawMeOnly(CC_DRAW_CONTEXT& context)
{
	if (m_thePointsAndTheirCellCodes.empty())
		return;

	if (!MACRO_Draw3D(context))
		return;
	
	//get the set of OpenGL functions (version 2.1)
	QOpenGLFunctions_2_1 *glFunc = context.glFunctions<QOpenGLFunctions_2_1>();
	assert( glFunc != nullptr );
	
	if ( glFunc == nullptr )
		return;
	
	bool pushName = MACRO_DrawEntityNames(context);

	if (pushName)
	{
		//not fast at all!
		if (MACRO_DrawFastNamesOnly(context))
			return;
		glFunc->glPushName(getUniqueIDForDisplay());
	}

	assert(m_displayedLevel < 256);
	RenderOctreeAs(context,
						m_displayType,
						this,
						static_cast<unsigned char>(m_displayedLevel),
						m_theAssociatedCloudAsGPC,
						m_glListID,
						m_shouldBeRefreshed);

	if (m_shouldBeRefreshed)
		m_shouldBeRefreshed = false;

	if (pushName)
		glFunc->glPopName();
}

/*** RENDERING METHODS ***/

void ccOctree::RenderOctreeAs(CC_DRAW_CONTEXT& context,
								CC_OCTREE_DISPLAY_TYPE octreeDisplayType,
								ccOctree* theOctree,
								unsigned char level,
								ccGenericPointCloud* theAssociatedCloud,
								int &octreeGLListID,
								bool updateOctreeGLDisplay)
{
	if (!theOctree || !theAssociatedCloud)
		return;
	
	//get the set of OpenGL functions (version 2.1)
	QOpenGLFunctions_2_1* glFunc = context.glFunctions<QOpenGLFunctions_2_1>();
	assert( glFunc != nullptr );
	
	if ( glFunc == nullptr )
		return;

	glFunc->glPushAttrib(GL_LIGHTING_BIT);

	if (octreeDisplayType == WIRE)
	{
		//cet affichage demande trop de memoire pour le stocker sous forme de liste OpenGL
		//donc on doit le generer dynamiquement
		
		glFunc->glDisable(GL_LIGHTING); //au cas où la lumiere soit allumee
		ccGL::Color3v(glFunc, ccColor::green.rgba);

		void* additionalParameters[] = {	reinterpret_cast<void*>(theOctree->m_frustrumIntersector),
											reinterpret_cast<void*>(glFunc)
		};
		theOctree->executeFunctionForAllCellsAtLevel(	level,
														&DrawCellAsABox,
														additionalParameters);
	}
	else
	{
		glDrawParams glParams;
		theAssociatedCloud->getDrawingParameters(glParams);

		if (glParams.showNorms)
		{
			//DGM: Strangely, when Qt::renderPixmap is called, the OpenGL version is sometimes 1.0!
			//DGM FIXME: is it still true with Qt5.4+?
#ifndef USE_QtOpenGL_CLASSES
			glFunc->glEnable((QGLFormat::openGLVersionFlags() & QGLFormat::OpenGL_Version_1_2 ? GL_RESCALE_NORMAL : GL_NORMALIZE));
#else
			glFunc->glDisable(GL_RESCALE_NORMAL);
#endif
			glFunc->glMaterialfv(GL_FRONT_AND_BACK,	GL_AMBIENT,		CC_DEFAULT_CLOUD_AMBIENT_COLOR.rgba  );
			glFunc->glMaterialfv(GL_FRONT_AND_BACK,	GL_SPECULAR,	CC_DEFAULT_CLOUD_SPECULAR_COLOR.rgba );
			glFunc->glMaterialfv(GL_FRONT_AND_BACK,	GL_DIFFUSE,		CC_DEFAULT_CLOUD_DIFFUSE_COLOR.rgba  );
			glFunc->glMaterialfv(GL_FRONT_AND_BACK,	GL_EMISSION,	CC_DEFAULT_CLOUD_EMISSION_COLOR.rgba );
			glFunc->glMaterialf (GL_FRONT_AND_BACK,	GL_SHININESS,	CC_DEFAULT_CLOUD_SHININESS);
			glFunc->glEnable(GL_LIGHTING);

			glFunc->glEnable(GL_COLOR_MATERIAL);
			glFunc->glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);
		}

		if (!glParams.showColors)
			ccGL::Color3v(glFunc, ccColor::white.rgba);

		if (updateOctreeGLDisplay || octreeGLListID < 0)
		{
			if (octreeGLListID < 0)
				octreeGLListID = glGenLists(1);
			else if (glIsList(octreeGLListID))
				glDeleteLists(octreeGLListID,1);
			glFunc->glNewList(octreeGLListID,GL_COMPILE);

			if (octreeDisplayType == MEAN_POINTS)
			{
				void* additionalParameters[] = {	reinterpret_cast<void*>(&glParams),
													reinterpret_cast<void*>(theAssociatedCloud),
													reinterpret_cast<void*>(glFunc)
				};

				glFunc->glBegin(GL_POINTS);
				theOctree->executeFunctionForAllCellsAtLevel(	level,
																&DrawCellAsAPoint,
																additionalParameters);
				glFunc->glEnd();
			}
			else
			{
				//by default we use a box as primitive
				PointCoordinateType cs = theOctree->getCellSize(level);
				CCVector3 dims(cs,cs,cs);
				ccBox box(dims);
				box.showColors(glParams.showColors || glParams.showSF);
				box.showNormals(glParams.showNorms);

				//trick: replace all normal indexes so that they point on the first one
				{
					if (box.arePerTriangleNormalsEnabled())
						for (unsigned i=0;i<box.size();++i)
							box.setTriangleNormalIndexes(i,0,0,0);
				}

				//fake context
				CC_DRAW_CONTEXT context;
				context.flags = CC_DRAW_3D | CC_DRAW_FOREGROUND| CC_LIGHT_ENABLED;
				context._win = 0;

				void* additionalParameters[] = {	reinterpret_cast<void*>(&glParams),
													reinterpret_cast<void*>(theAssociatedCloud),
													reinterpret_cast<void*>(&box),
													reinterpret_cast<void*>(&context)
				};

				theOctree->executeFunctionForAllCellsAtLevel(	level,
																&DrawCellAsAPrimitive,
																additionalParameters);
			}

			glFunc->glEndList();
		}

		glFunc->glCallList(octreeGLListID);

		if (glParams.showNorms)
		{
			glFunc->glDisable(GL_COLOR_MATERIAL);
			//DGM FIXME: is it still true with Qt5.4+?
#ifndef USE_QtOpenGL_CLASSES
			glFunc->glDisable((QGLFormat::openGLVersionFlags() & QGLFormat::OpenGL_Version_1_2 ? GL_RESCALE_NORMAL : GL_NORMALIZE));
#else
			glFunc->glDisable(GL_RESCALE_NORMAL);
#endif
			glFunc->glDisable(GL_LIGHTING);
		}
	}

	glFunc->glPopAttrib();
}

bool ccOctree::DrawCellAsABox(	const CCLib::DgmOctree::octreeCell& cell,
								void** additionalParameters,
								CCLib::NormalizedProgress* nProgress/*=0*/)
{
	ccOctreeFrustrumIntersector* ofi = static_cast<ccOctreeFrustrumIntersector*>(additionalParameters[0]);
	QOpenGLFunctions_2_1* glFunc     = static_cast<QOpenGLFunctions_2_1*>(additionalParameters[1]);
	assert(glFunc != nullptr);

	CCVector3 bbMin, bbMax;
	cell.parentOctree->computeCellLimits(cell.truncatedCode, cell.level, bbMin, bbMax, true);

	ccOctreeFrustrumIntersector::OctreeCellVisibility vis = ccOctreeFrustrumIntersector::CELL_OUTSIDE_FRUSTRUM;
	if (ofi)
		vis = ofi->positionFromFrustum(cell.truncatedCode, cell.level);

	// outside
	if (vis == ccOctreeFrustrumIntersector::CELL_OUTSIDE_FRUSTRUM)
	{
		ccGL::Color3v(glFunc, ccColor::green.rgba);
	}
	else
	{
		glPushAttrib(GL_LINE_BIT);
		glLineWidth(2.0f);
		// inside
		if (vis == ccOctreeFrustrumIntersector::CELL_INSIDE_FRUSTRUM)
			ccGL::Color3v(glFunc, ccColor::magenta.rgba);
		// intersecting
		else
			ccGL::Color3v(glFunc, ccColor::blue.rgba);
	}

	glFunc->glBegin(GL_LINE_LOOP);
	ccGL::Vertex3v(glFunc, bbMin.u);
	ccGL::Vertex3(glFunc, bbMax.x,bbMin.y,bbMin.z);
	ccGL::Vertex3(glFunc, bbMax.x,bbMax.y,bbMin.z);
	ccGL::Vertex3(glFunc, bbMin.x,bbMax.y,bbMin.z);
	glFunc->glEnd();

	glFunc->glBegin(GL_LINE_LOOP);
	ccGL::Vertex3(glFunc, bbMin.x,bbMin.y,bbMax.z);
	ccGL::Vertex3(glFunc, bbMax.x,bbMin.y,bbMax.z);
	ccGL::Vertex3v(glFunc, bbMax.u);
	ccGL::Vertex3(glFunc, bbMin.x, bbMax.y, bbMax.z);
	glFunc->glEnd();

	glFunc->glBegin(GL_LINES);
	ccGL::Vertex3v(glFunc, bbMin.u);
	ccGL::Vertex3(glFunc, bbMin.x,bbMin.y,bbMax.z);
	ccGL::Vertex3(glFunc, bbMax.x,bbMin.y,bbMin.z);
	ccGL::Vertex3(glFunc, bbMax.x,bbMin.y,bbMax.z);
	ccGL::Vertex3(glFunc, bbMax.x,bbMax.y,bbMin.z);
	ccGL::Vertex3v(glFunc, bbMax.u);
	ccGL::Vertex3(glFunc, bbMin.x,bbMax.y,bbMin.z);
	ccGL::Vertex3(glFunc, bbMin.x,bbMax.y,bbMax.z);
	glFunc->glEnd();

	// not outside
	if (vis != ccOctreeFrustrumIntersector::CELL_OUTSIDE_FRUSTRUM)
	{
		glFunc->glPopAttrib();
	}

	return true;
}

bool ccOctree::DrawCellAsAPoint(const CCLib::DgmOctree::octreeCell& cell,
								void** additionalParameters,
								CCLib::NormalizedProgress* nProgress/*=0*/)
{
	//variables additionnelles
	glDrawParams* glParams						= reinterpret_cast<glDrawParams*>(additionalParameters[0]);
	ccGenericPointCloud* theAssociatedCloud		= reinterpret_cast<ccGenericPointCloud*>(additionalParameters[1]);
	QOpenGLFunctions_2_1* glFunc                = static_cast<QOpenGLFunctions_2_1*>(additionalParameters[2]);
	assert(glFunc != nullptr);

	if (glParams->showSF)
	{
		ScalarType dist = CCLib::ScalarFieldTools::computeMeanScalarValue(cell.points);
		const ColorCompType* col = theAssociatedCloud->geScalarValueColor(dist);
		glFunc->glColor3ubv(col ? col : ccColor::lightGrey.rgba);
	}
	else if (glParams->showColors)
	{
		ColorCompType col[3];
		ComputeAverageColor(cell.points, theAssociatedCloud, col);
		glFunc->glColor3ubv(col);
	}

	if (glParams->showNorms)
	{
		CCVector3 N = ComputeAverageNorm(cell.points, theAssociatedCloud);
		ccGL::Normal3v(glFunc, N.u);
	}

	const CCVector3* gravityCenter = CCLib::Neighbourhood(cell.points).getGravityCenter();
	ccGL::Vertex3v(glFunc, gravityCenter->u);

	return true;
}

bool ccOctree::DrawCellAsAPrimitive(const CCLib::DgmOctree::octreeCell& cell,
									void** additionalParameters,
									CCLib::NormalizedProgress* nProgress/*=0*/)
{
	//variables additionnelles
	glDrawParams* glParams						= reinterpret_cast<glDrawParams*>(additionalParameters[0]);
	ccGenericPointCloud* theAssociatedCloud		= reinterpret_cast<ccGenericPointCloud*>(additionalParameters[1]);
	ccGenericPrimitive*	primitive				= reinterpret_cast<ccGenericPrimitive*>(additionalParameters[2]);
	CC_DRAW_CONTEXT* context					= reinterpret_cast<CC_DRAW_CONTEXT*>(additionalParameters[3]);

	//get the set of OpenGL functions (version 2.1)
	QOpenGLFunctions_2_1* glFunc = context->glFunctions<QOpenGLFunctions_2_1>();
	assert(glFunc != nullptr);

	if (glFunc == nullptr)
		return false;

	CCVector3 cellCenter;
	cell.parentOctree->computeCellCenter(cell.truncatedCode, cell.level, cellCenter, true);

	if (glParams->showSF)
	{
		ScalarType dist = CCLib::ScalarFieldTools::computeMeanScalarValue(cell.points);
		ccColor::Rgba rgb(theAssociatedCloud->geScalarValueColor(dist));
		primitive->setColor(rgb);
	}
	else if (glParams->showColors)
	{
		ccColor::Rgb col;
		ComputeAverageColor(cell.points, theAssociatedCloud, col.rgb);
		primitive->setColor(col);
	}

	if (glParams->showNorms)
	{
		CCVector3 N = ComputeAverageNorm(cell.points,theAssociatedCloud);
		if (primitive->getTriNormsTable())
		{
			//only one normal!
			primitive->getTriNormsTable()->setValue(0,ccNormalVectors::GetNormIndex(N.u));
		}
	}

	glFunc->glPushMatrix();
	ccGL::Translate(glFunc, cellCenter.x, cellCenter.y, cellCenter.z);
	primitive->draw(*context);
	glFunc->glPopMatrix();

	return true;
}

void ccOctree::ComputeAverageColor(CCLib::ReferenceCloud* subset, ccGenericPointCloud* sourceCloud, ColorCompType meanCol[])
{
	if (!subset || subset->size() == 0 || !sourceCloud)
		return;

	assert(sourceCloud->hasColors());
	assert(subset->getAssociatedCloud() == static_cast<CCLib::GenericIndexedCloud*>(sourceCloud));

	Tuple3Tpl<double> sum(0, 0, 0);

	unsigned n = subset->size();
	for (unsigned i = 0; i < n; ++i)
	{
		const ColorCompType* _theColors = sourceCloud->getPointColor(subset->getPointGlobalIndex(i));
		sum.x += static_cast<double>(*_theColors++);
		sum.y += static_cast<double>(*_theColors++);
		sum.z += static_cast<double>(*_theColors++);
	}

	meanCol[0] = static_cast<ColorCompType>(sum.x / n);
	meanCol[1] = static_cast<ColorCompType>(sum.y / n);
	meanCol[2] = static_cast<ColorCompType>(sum.z / n);
}

CCVector3 ccOctree::ComputeAverageNorm(CCLib::ReferenceCloud* subset, ccGenericPointCloud* sourceCloud)
{
	CCVector3 N(0,0,0);

	if (!subset || subset->size() == 0 || !sourceCloud)
		return N;

	assert(sourceCloud->hasNormals());
	assert(subset->getAssociatedCloud() == static_cast<CCLib::GenericIndexedCloud*>(sourceCloud));

	unsigned n = subset->size();
	for (unsigned i=0; i<n; ++i)
	{
		const CCVector3& Ni = sourceCloud->getPointNormal(subset->getPointGlobalIndex(i));
		N += Ni;
	}

	N.normalize();
	return N;
}

bool ccOctree::intersectWithFrustrum(ccCameraSensor* sensor, std::vector<unsigned>& inCameraFrustrum)
{
	if (!sensor)
		return false;

	// initialization
	float globalPlaneCoefficients[6][4];
	CCVector3 globalCorners[8];
	CCVector3 globalEdges[6];
	CCVector3 globalCenter; 
	sensor->computeGlobalPlaneCoefficients(globalPlaneCoefficients, globalCorners, globalEdges, globalCenter);

	if (!m_frustrumIntersector)
	{
		m_frustrumIntersector = new ccOctreeFrustrumIntersector();
		if (!m_frustrumIntersector->build(this))
		{
			ccLog::Warning("[ccOctree::intersectWithFrustrum] Not enough memory!");
			return false;
		}
	}

	// get points of cells in frustrum
	std::vector< std::pair<unsigned, CCVector3> > pointsToTest;
	m_frustrumIntersector->computeFrustumIntersectionWithOctree(pointsToTest, inCameraFrustrum, globalPlaneCoefficients, globalCorners, globalEdges, globalCenter);
	
	// project points
	for (size_t i=0; i<pointsToTest.size(); i++)
	{
		if (sensor->isGlobalCoordInFrustrum(pointsToTest[i].second/*, false*/))
			inCameraFrustrum.push_back(pointsToTest[i].first);
	}

	return true;
}

bool ccOctree::pointPicking(const CCVector2d& clickPos,
							const ccGLCameraParameters& camera,
							PointDescriptor& output,
							double pickWidth_pix/*=3.0*/) const
{
	output.point = 0;
	output.squareDistd = -1.0;

	if (!m_theAssociatedCloudAsGPC)
	{
		assert(false);
		return false;
	}

	if (m_thePointsAndTheirCellCodes.empty())
	{
		//nothing to do
		assert(false);
		return false;
	}
	
	CCVector3d clickPosd(clickPos.x, clickPos.y, 0);
	CCVector3d X(0,0,0);
	if (!camera.unproject(clickPosd, X))
	{
		return false;
	}

	//compute 3D picking 'ray'
	CCVector3 rayAxis, rayOrigin;
	{
		CCVector3d clickPosd2(clickPos.x, clickPos.y, 1);
		CCVector3d Y(0,0,0);
		if (!camera.unproject(clickPosd2, Y))
		{
			return false;
		}

		CCVector3d dir = Y-X;
		dir.normalize();
		rayAxis = CCVector3::fromArray(dir.u);
		rayOrigin = CCVector3::fromArray(X.u);

		ccGLMatrix trans;
		if (m_theAssociatedCloudAsGPC->getAbsoluteGLTransformation(trans))
		{
			trans.invert();
			trans.applyRotation(rayAxis);
			trans.apply(rayOrigin);
		}
	}

	CCVector3 margin(0, 0, 0);
	double maxSqRadius = 0;
	double maxFOV_rad = 0;
	if (camera.perspective)
	{
		maxFOV_rad = 0.002 * pickWidth_pix; //empirical conversion from pixels to FOV angle (in radians)
	}
	else
	{
		double maxRadius = pickWidth_pix * camera.pixelSize / 2;
		margin = CCVector3(1, 1, 1) * static_cast<PointCoordinateType>(maxRadius);
		maxSqRadius = maxRadius*maxRadius;
	}

	//first test with the total bounding box
	Ray<PointCoordinateType> ray(rayAxis, rayOrigin);
	if (!AABB<PointCoordinateType>(m_dimMin - margin, m_dimMax + margin).intersects(ray))
	{
		//no intersection
		return false;
	}

	//no need to go too deep
	const unsigned char maxLevel = findBestLevelForAGivenPopulationPerCell(10);

	//starting level of subdivision
	unsigned char level = 1;
	//binary shift for cell code truncation at current level
	unsigned char currentBitDec = GET_BIT_SHIFT(level);
	//current cell code
	OctreeCellCodeType currentCode = INVALID_CELL_CODE;
	//whether the current cell should be skipped or not
	bool skipThisCell = false;

#ifdef _DEBUG
	m_theAssociatedCloud->enableScalarField();
#endif

	//ray with origin expressed in the local coordinate system!
	Ray<PointCoordinateType> rayLocal(rayAxis, rayOrigin - m_dimMin);

	//let's sweep through the octree
	for (cellsContainer::const_iterator it = m_thePointsAndTheirCellCodes.begin(); it != m_thePointsAndTheirCellCodes.end(); ++it)
	{
		OctreeCellCodeType truncatedCode = (it->theCode >> currentBitDec);
		
		//new cell?
		if (truncatedCode != (currentCode >> currentBitDec))
		{
			//look for the biggest 'parent' cell that englobes this cell and the previous one (if any)
			while (level > 1)
			{
				unsigned char bitDec = GET_BIT_SHIFT(level-1);
				if ((it->theCode >> bitDec) == (currentCode >> bitDec))
				{
					//same parent cell, we can stop here
					break;
				}
				--level;
			}

			currentCode = it->theCode;

			//now try to go deeper with the new cell
			while (level < maxLevel)
			{
				Tuple3i cellPos;
				getCellPos(it->theCode, level, cellPos, false);

				//first test with the total bounding box
				const PointCoordinateType& halfCellSize = getCellSize(level) / 2;
				CCVector3 cellCenter(	(2* cellPos.x + 1) * halfCellSize,
										(2* cellPos.y + 1) * halfCellSize,
										(2* cellPos.z + 1) * halfCellSize);

				CCVector3 halfCell = CCVector3(halfCellSize, halfCellSize, halfCellSize);

				if (camera.perspective)
				{
					double radialSqDist, sqDistToOrigin;
					rayLocal.squareDistances(cellCenter, radialSqDist, sqDistToOrigin);

					double dx = sqrt(sqDistToOrigin);
					double dy = std::max<double>(0, sqrt(radialSqDist) - SQRT_3 * halfCellSize);
					double fov_rad = atan2(dy, dx);

					skipThisCell = (fov_rad > maxFOV_rad);
				}
				else
				{
					skipThisCell = !AABB<PointCoordinateType>(	cellCenter - halfCell - margin,
																cellCenter + halfCell + margin).intersects(rayLocal);
				}

				if (skipThisCell)
					break;
				else
					++level;
			}
			currentBitDec = GET_BIT_SHIFT(level);
		}

#ifdef _DEBUG
		m_theAssociatedCloud->setPointScalarValue(it->theIndex, level);
#endif

		if (!skipThisCell)
		{
			//test the point
			const CCVector3* P = m_theAssociatedCloud->getPoint(it->theIndex);

			CCVector3d Q2D;
			camera.project(*P, Q2D);

			if (	fabs(Q2D.x - clickPos.x) <= pickWidth_pix
				&&	fabs(Q2D.y - clickPos.y) <= pickWidth_pix )
			{
				double squareDist = CCVector3d(X.x - P->x, X.y - P->y, X.z - P->z).norm2d();
				if (!output.point || squareDist < output.squareDistd)
				{
					output.point = P;
					output.pointIndex = it->theIndex;
					output.squareDistd = squareDist;
				}
			}
		}
	}

	return true;
}
