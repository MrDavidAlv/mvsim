/*+-------------------------------------------------------------------------+
  |                       MultiVehicle simulator (libmvsim)                 |
  |                                                                         |
  | Copyright (C) 2014-2023  Jose Luis Blanco Claraco                       |
  | Copyright (C) 2017  Borys Tymchenko (Odessa Polytechnic University)     |
  | Distributed under 3-clause BSD License                                  |
  |   See COPYING                                                           |
  +-------------------------------------------------------------------------+ */

#include "CollisionShapeCache.h"

#include <mrpt/opengl/CAssimpModel.h>
#include <mrpt/version.h>

using namespace mvsim;

CollisionShapeCache& CollisionShapeCache::Instance()
{
	static CollisionShapeCache o;
	return o;
}

Shape2p5 CollisionShapeCache::get(
	mrpt::opengl::CRenderizable& obj, float zMin, float zMax,
	const mrpt::poses::CPose3D& modelPose, const float modelScale,
	const std::optional<std::string>& modelFile)
{
	// already cached?
	if (modelFile)
	{
		if (auto it = cache.find(modelFile.value()); it != cache.end())
			return it->second.convexHull;
	}

	// No, it's a new model path, create its placeholder:
	Shape2p5 retVal;
	Shape2p5& ret = modelFile ? cache[*modelFile].convexHull : retVal;

	// Make sure the points and vertices buffers are up to date, so we can
	// access them:
	auto* oAssimp = dynamic_cast<mrpt::opengl::CAssimpModel*>(&obj);
	if (oAssimp)
	{
		oAssimp->onUpdateBuffers_all();
	}
	auto* oRSWF =
		dynamic_cast<mrpt::opengl::CRenderizableShaderWireFrame*>(&obj);
	if (oRSWF)
	{
		oRSWF->onUpdateBuffers_Wireframe();
	}
	auto* oRST =
		dynamic_cast<mrpt::opengl::CRenderizableShaderTriangles*>(&obj);
	if (oRST)
	{
		oRST->onUpdateBuffers_Triangles();
	}
	auto* oRSTT =
		dynamic_cast<mrpt::opengl::CRenderizableShaderTexturedTriangles*>(&obj);
	if (oRSTT)
	{
		oRSTT->onUpdateBuffers_TexturedTriangles();
	}
	auto* oRP = dynamic_cast<mrpt::opengl::CRenderizableShaderPoints*>(&obj);
	if (oRP)
	{
		oRP->onUpdateBuffers_Points();
	}

	// Slice bbox in z up to a given relevant height:
	size_t numTotalPts = 0, numPassedPts = 0;

	std::vector<mrpt::math::TPoint3Df> allPts;

	auto lambdaUpdatePt = [&](const mrpt::math::TPoint3Df& orgPt) {
		numTotalPts++;
		auto pt = modelPose.composePoint(orgPt * modelScale);
		if (pt.z < zMin || pt.z > zMax) return;	 // skip
		allPts.push_back(pt);
		numPassedPts++;
	};

	if (oRST)
	{
		auto lck = mrpt::lockHelper(oRST->shaderTrianglesBufferMutex().data);
		const auto& tris = oRST->shaderTrianglesBuffer();
		for (const auto& tri : tris)
			for (const auto& v : tri.vertices) lambdaUpdatePt(v.xyzrgba.pt);
	}
	if (oRSTT)
	{
		auto lck =
			mrpt::lockHelper(oRSTT->shaderTexturedTrianglesBufferMutex().data);
		const auto& tris = oRSTT->shaderTexturedTrianglesBuffer();
		for (const auto& tri : tris)
			for (const auto& v : tri.vertices) lambdaUpdatePt(v.xyzrgba.pt);
	}
	if (oRP)
	{
		auto lck = mrpt::lockHelper(oRP->shaderPointsBuffersMutex().data);
		const auto& pts = oRP->shaderPointsVertexPointBuffer();
		for (const auto& pt : pts) lambdaUpdatePt(pt);
	}
	if (oRSWF)
	{
		auto lck = mrpt::lockHelper(oRSWF->shaderWireframeBuffersMutex().data);
		const auto& pts = oRSWF->shaderWireframeVertexPointBuffer();
		for (const auto& pt : pts) lambdaUpdatePt(pt);
	}

#if MRPT_VERSION >= 0x260
	if (oAssimp)
	{
		const auto& txtrdObjs = oAssimp->texturedObjects();	 // mrpt>=2.6.0
		for (const auto& o : txtrdObjs)
		{
			if (!o) continue;

			auto lck =
				mrpt::lockHelper(o->shaderTexturedTrianglesBufferMutex().data);
			const auto& tris = o->shaderTexturedTrianglesBuffer();
			for (const auto& tri : tris)
				for (const auto& v : tri.vertices) lambdaUpdatePt(v.xyzrgba.pt);
		}
	}
#endif

	// Convert all points into an actual 2.5 volume:
	// ---------------------------------------------------------
	ret = Shape2p5::CreateConvexHullFromPoints(allPts);

	const auto vol = ret.volume();

#if 0
	std::cout << "shape2.5 for ["
			  << (modelFile.has_value() ? *modelFile : "none")
			  << "] glClass=" << obj.GetRuntimeClass()->className
			  << " numTotalPts=" << numTotalPts
			  << " numPassedPts=" << numPassedPts << " zMin = " << zMin
			  << " zMax=" << zMax << " shape=" << ret.contour.size() << " pts, "
			  << " volume=" << vol << "\n";
#endif

	if (vol < 1e-8)
	{
		THROW_EXCEPTION_FMT(
			"Error: Collision volume for visual model ('%s') has almost null "
			"volume (=%g m³). A possible cause, if this is a <block>, is not "
			"enough vertices within the given range [zmin,zmax]",
			modelFile.has_value() ? modelFile->c_str() : "none", vol);
	}

	return ret;
}