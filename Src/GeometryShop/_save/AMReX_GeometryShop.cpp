/*
 *       {_       {__       {__{_______              {__      {__
 *      {_ __     {_ {__   {___{__    {__             {__   {__  
 *     {_  {__    {__ {__ { {__{__    {__     {__      {__ {__   
 *    {__   {__   {__  {__  {__{_ {__       {_   {__     {__     
 *   {______ {__  {__   {_  {__{__  {__    {_____ {__  {__ {__   
 *  {__       {__ {__       {__{__    {__  {_         {__   {__  
 * {__         {__{__       {__{__      {__  {____   {__      {__
 *
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "AMReX_GeometryShop.H"
#include "AMReX_RealVect.H"
#include "AMReX.H"
#include "AMReX_StdSetIVS.H"
#include "AMReX_BoxIterator.H"


namespace amrex
{

  GeometryShop::GeometryShop(const BaseIF& a_localGeom,
                             int           a_verbosity,
                             Real          a_thrshdVoF)
  {




    m_implicitFunction = a_localGeom.newImplicitFunction();

    m_verbosity = a_verbosity;

    Real arg1 = 10.0;
    Real arg2 = -m_verbosity;

    m_threshold = 1.0e-15*pow(arg1, arg2);


    m_thrshdVoF = a_thrshdVoF;

  }

  GeometryShop::~GeometryShop()
  {
    delete(m_implicitFunction);
  }


  /**********************************************/
  GeometryShop::InOut GeometryShop::InsideOutside(const Box&           a_region,
                                                  const Box&           a_domain,
                                                  const RealVect&      a_origin,
                                                  const Real&          a_dx) const
  {
    GeometryShop::InOut rtn;


    // All corner indices for the current box
    Box allCorners(a_region);
    allCorners.surroundingNodes();

    RealVect physCorner(allCorners.smallEnd());

    physCorner *= a_dx;
    physCorner += a_origin;

    Real firstValue;
    Real firstSign;

    firstValue = m_implicitFunction->value(physCorner);
    firstSign  = copysign(1.0, firstValue);

    if ( firstSign < 0 )
      {
        rtn = GeometryShop::Regular;
      }
    else
      {
        rtn = GeometryShop::Covered;
      }

    

    for (BoxIterator bit(allCorners); bit.ok(); ++bit)
      {
        // Current corner
        IntVect corner = bit();

        Real functionValue;
        Real functionSign;

        // Compute physical coordinate of corner
        for (int idir = 0; idir < SpaceDim; ++idir)
          {
            physCorner[idir] = a_dx*corner[idir] + a_origin[idir];
          }

        // If the implicit function value is positive then the current
        // corner is outside the domain
        functionValue = m_implicitFunction->value(physCorner);

        functionSign = copysign(1.0, functionValue);

        if (functionValue == 0 || firstValue == 0)
          {
            if (functionSign * firstSign < 0)
              {
                rtn = GeometryShop::Irregular;
                return rtn;
              }
          }
        if (functionValue * firstValue < 0.0 )
          {
            rtn = GeometryShop::Irregular;
            return rtn;
          }
      }
    return rtn;
  }

  /*********************************************/
  void
  GeometryShop::fillGraph(BaseFab<int>        & a_regIrregCovered,
                          std::vector<IrregNode>   & a_nodes,
                          const Box           & a_validRegion,
                          const Box           & a_ghostRegion,
                          const Box & a_domain,
                          const RealVect      & a_origin,
                          const Real          & a_dx) const
  {
    assert(a_domain.contains(a_ghostRegion));
    a_nodes.resize(0);
    a_regIrregCovered.resize(a_ghostRegion, 1);


    Real thrshd = m_thrshdVoF;

    std::vector<IntVect> ivsirreg;
    std::vector<IntVect> ivsdrop ;
    long int numCovered=0, numReg=0, numIrreg=0;

    //IntVect ivdeblo(D_DECL(62,510,0));
    //IntVect ivdebhi(D_DECL(63,513,0));
    //Box debbox(ivdeblo, ivdebhi);

    for (BoxIterator bit(a_ghostRegion); bit.ok(); ++bit)
      {
        const IntVect iv =bit();
        //int istop = 0;
        //if(debbox.contains(iv))
        //  {
        //    istop = 1;
        //  }

        Box miniBox(iv, iv);
        GeometryShop::InOut inout = InsideOutside(miniBox, a_domain, a_origin, a_dx);

        if (inout == GeometryShop::Covered)
          {
            // set covered cells to -1
            a_regIrregCovered(iv, 0) = -1;
            numCovered++;
          }
        else if (inout == GeometryShop::Regular)
          {
            // set regular cells to 1
            a_regIrregCovered(iv, 0) =  1;
            numReg++;
          }
        else
          {
            // set irregular cells to 0
            a_regIrregCovered(iv, 0) =  0;
            if (a_validRegion.contains(iv))
              {
                ivsirreg.push_back(iv);
                numIrreg++;
              }
          }
      }

    //if a regular is next to a  covered, change to irregular with correct arcs and so on.
    for (BoxIterator bit(a_ghostRegion); bit.ok(); ++bit)
      {
        const IntVect iv =bit();
        // int istop = 0;
        // if(debbox.contains(iv))
        //   {
        //     istop = 1;
        //   }

        if(a_regIrregCovered(iv, 0) == -1)
          {
            for(int idir = 0; idir < SpaceDim; idir++)
              {
                for(SideIterator sit; sit.ok(); ++sit)
                  {
                    int ishift = sign(sit());
                    IntVect ivshift = iv + ishift*BASISV(idir);
                    int bfvalshift = -1;
                    if(a_ghostRegion.contains(ivshift))
                      {
                        bfvalshift = a_regIrregCovered(ivshift, 0);
                      }
                    if(bfvalshift  == 1)
                      {
                        a_regIrregCovered(ivshift, 0) =  0;

                        if(a_validRegion.contains(ivshift))
                          {
                            IrregNode newNode;
                            getFullNodeNextToCovered(newNode, 
                                                     a_regIrregCovered,
                                                     ivshift, 
                                                     a_domain);
                            a_nodes.push_back(newNode);
                          }

                      }
                  }
              }
          }
      }

    // now loop through irregular cells and make nodes for each  one.
    for(int ivec = 0; ivec < ivsirreg.size(); ivec++)
      {
        const IntVect& iv = ivsirreg[ivec];

        //int istop = 0;
        //if(debbox.contains(iv))
        //  {
        //    istop = 1;
        //  }

        Real     volFrac, bndryArea;
        RealVect normal, volCentroid, bndryCentroid;
        std::vector<int> loArc[SpaceDim];
        std::vector<int> hiArc[SpaceDim];
        std::vector<Real> loAreaFrac[SpaceDim];
        std::vector<Real> hiAreaFrac[SpaceDim];
        std::vector<RealVect> loFaceCentroid[SpaceDim];
        std::vector<RealVect> hiFaceCentroid[SpaceDim];
        computeVoFInternals(volFrac,
                            loArc,
                            hiArc,
                            loAreaFrac,
                            hiAreaFrac,
                            bndryArea,
                            normal,
                            volCentroid,
                            bndryCentroid,
                            loFaceCentroid,
                            hiFaceCentroid,
                            a_regIrregCovered,
                            a_domain,
                            a_origin,
                            a_dx,
                            iv);


        if (thrshd > 0. && volFrac < thrshd)
          {
            ivsdrop.push_back(iv);
            a_regIrregCovered(iv, 0) = -1;
            if (m_verbosity > 2)
              {
                std::cout << "Removing vof " << iv << " with volFrac " << volFrac << std::endl;
              }
          }//CP record these nodes to be removed
        else
          {
            IrregNode newNode;
            newNode.m_cell          = iv;
            newNode.m_volFrac       = volFrac;
            newNode.m_cellIndex     = 0;
            newNode.m_volCentroid   = volCentroid;
            newNode.m_bndryCentroid = bndryCentroid;

            for (int faceDir = 0; faceDir < SpaceDim; faceDir++)
              {
                int loNodeInd = newNode.index(faceDir, Side::Lo);
                int hiNodeInd = newNode.index(faceDir, Side::Hi);
                newNode.m_arc[loNodeInd]          = loArc[faceDir];
                newNode.m_arc[hiNodeInd]          = hiArc[faceDir];
                newNode.m_areaFrac[loNodeInd]     = loAreaFrac[faceDir];
                newNode.m_areaFrac[hiNodeInd]     = hiAreaFrac[faceDir];
                newNode.m_faceCentroid[loNodeInd] = loFaceCentroid[faceDir];
                newNode.m_faceCentroid[hiNodeInd] = hiFaceCentroid[faceDir];
              }
            a_nodes.push_back(newNode);
          }

      } // end loop over cut cells in the box

    // CP: fix sweep that removes cells with volFrac less than a certain threshold
    for(int ivec = 0; ivec < ivsdrop.size(); ivec++)
      {
        const IntVect& iv = ivsdrop[ivec];
  
        for (int faceDir = 0; faceDir < SpaceDim; faceDir++)
          {
            for (SideIterator sit; sit.ok(); ++sit)
              {
                int isign = sign(sit());
                IntVect otherIV = iv + isign*BASISV(faceDir);
                if (a_validRegion.contains(otherIV))
                  {
                    if (a_regIrregCovered(otherIV,0) == 0)
                      {
                        // i am in the case where the other cell
                        // is also irregular.   I just made a previously
                        // irregular cell covered so I have to check to
                        // see if it had any faces pointed this way.
                        int inode = -1;
                        bool found = false;
                        for (int ivec = 0; ivec < a_nodes.size() && ! found; ivec++)
                          {
                            if (a_nodes[ivec].m_cell == otherIV)
                              {
                                inode = ivec;
                                found = true;
                              }
                          }
                        if (!found && a_validRegion.contains(otherIV))
                          {
                            amrex::Abort("something wrong in our logic");
                          }
                        if (found)
                          {
                            int arcindex = a_nodes[inode].index(faceDir, flip(sit()));
                            a_nodes[inode].m_arc[         arcindex].resize(0);
                            a_nodes[inode].m_areaFrac[    arcindex].resize(0);
                            a_nodes[inode].m_faceCentroid[arcindex].resize(0);
                          }
                      }
                    else if (a_regIrregCovered(otherIV,0) == 1)
                      {
                        a_regIrregCovered(otherIV, 0) = 0;
                        IrregNode newNode;
                        getFullNodeNextToCovered(newNode, 
                                                 a_regIrregCovered,
                                                 otherIV, 
                                                 a_domain);
                        a_nodes.push_back(newNode);

                      }//else if
                  }//valid region
              }//sit
          }//facedir
      }//ivsdrop
    std::cout << "numIrreg  = " << numIrreg << std::endl;
    std::cout << "number of nodes  = " << a_nodes.size() << std::endl;
  }

  /**********************************************/
  void
  GeometryShop::
  getFullNodeNextToCovered(IrregNode            & a_newNode, 
                           const BaseFab<int>   & a_regIrregCovered,
                           const IntVect        & a_iv,
                           const Box  & a_domain) const
  {

    a_newNode.m_cell          = a_iv;
    a_newNode.m_volFrac       = 1.0;
    a_newNode.m_cellIndex     = 0;
    a_newNode.m_volCentroid   = RealVect::Zero;
    //set regular cell values then fix up
    a_newNode.m_bndryCentroid = RealVect::Zero;
    int coveredDir;
    Side::LoHiSide coveredSide;
    bool found = false;

    for (int faceDir = 0; faceDir < SpaceDim; faceDir++)
      {
        for(SideIterator sit; sit.ok(); ++sit)
          {
            int ishift = sign(sit());
            IntVect ivshift = a_iv + ishift*BASISV(faceDir);
            std::vector<int> arc;
            std::vector<Real> areaFrac;
            std::vector<RealVect> faceCentroid;
            if(!a_domain.contains(ivshift))
              {
                // boundary arcs always -1
                arc.resize(1,-1);
                areaFrac.resize(1, 1.0);
                faceCentroid.resize(1, RealVect::Unit);
              }
            else if (a_regIrregCovered(ivshift, 0) >= 0)
              {
                //irregular cell or regular cell
                //compute vof internals returns something special if 
                //connected to a regular cell but EBGraph treats both the  same.
                //it just  knows that the cell index of a regular cell is 0
                arc.resize(1,0);
                areaFrac.resize(1, 1.0);
                faceCentroid.resize(1, RealVect::Unit);
              }
            else if (a_regIrregCovered(ivshift, 0) < 0)
              {
                found = true;
                coveredDir= faceDir;
                coveredSide = sit();
                // covered face!
                arc.resize(0);
                areaFrac.resize(0);
                faceCentroid.resize(0);
              }
            else
              {
                amrex::Error("logic error");
              }
          
            int nodeInd = a_newNode.index(faceDir, sit());
            a_newNode.m_arc[nodeInd]          = arc;
            a_newNode.m_areaFrac[nodeInd]     = areaFrac;
            a_newNode.m_faceCentroid[nodeInd] = faceCentroid;
          }
      }
    //fix boundary centroid
    if(found)
      {
        int centsign = sign(coveredSide);
        a_newNode.m_bndryCentroid[coveredDir] =  centsign*0.5;
      }
  }
  /*********************************************/
  void
  GeometryShop::computeVoFInternals(Real&               a_volFrac,
                                    std::vector<int>         a_loArc[SpaceDim],
                                    std::vector<int>         a_hiArc[SpaceDim],
                                    std::vector<Real>        a_loAreaFrac[SpaceDim],
                                    std::vector<Real>        a_hiAreaFrac[SpaceDim],
                                    Real&               a_bndryArea,
                                    RealVect&           a_normal,
                                    RealVect&           a_volCentroid,
                                    RealVect&           a_bndryCentroid,
                                    std::vector<RealVect>    a_loFaceCentroid[SpaceDim],
                                    std::vector<RealVect>    a_hiFaceCentroid[SpaceDim],
                                    const BaseFab<int>& a_regIrregCovered,
                                    const Box&a_domain,
                                    const RealVect&     a_origin,
                                    const Real&         a_dx,
                                    const IntVect&      a_iv)const
  {

    // need maxDx to properly scale a_bndryArea
    Real maxDx = a_dx;


    if (SpaceDim == 2)
      {
        // In 2D a vof is a faceMo = edgeMo[4],boundary length and normal vector
        faceMo Face;
        edgeMo edges[4];

        bool faceCovered;
        bool faceRegular;
        bool faceDontKnow;
        int faceNormal = 2;

        // get edgeType and intersection points
        edgeData2D(edges,
                   faceCovered,
                   faceRegular,
                   faceDontKnow,
                   a_dx,
                   a_iv,
                   a_domain,
                   a_origin);

        assert(faceRegular || faceCovered || faceDontKnow);
        assert((!(faceRegular && faceCovered)) && (!(faceRegular && faceDontKnow)) && (!(faceDontKnow && faceCovered)));
        // define the faceMo
        Face.define(edges,faceNormal,faceCovered,faceRegular,faceDontKnow);

        Moments geom;

        // answer0,answer1 are vectors whose components contain geometric information
        std::vector<Real> answer0;
        std::vector<Real> answer1;

        int order = 0;
        answer0 = geom.momentCalc2D(order,Face);

        // extract the info from answer0 and answer1

        // volfrac
        a_volFrac = answer0[0];

        order = 1;
        answer1 = geom.momentCalc2D(order,Face);

        // centroid
        for (int idir=0; idir<SpaceDim;++idir)
          {
            a_volCentroid[idir] = answer1[SpaceDim-1-idir];
          }

        if (a_volFrac <= 0.0)
          {
            a_volCentroid = RealVect::Zero;
          }
        else
          {
            a_volCentroid /= a_volFrac;
          }

        // normal
        Real normalVec[SpaceDim];
        Face.getNormal(normalVec);
        for (int idir = 0;idir < SpaceDim;++idir)
          {
            a_normal[idir] = normalVec[idir];
          }

        for (int idir = 0;idir < SpaceDim;++idir)
          {
            // (nx,ny)->(nxdy,nydx)
            a_normal[idir] = normalVec[idir]*a_dx;
          }
        Real anisBd = 0.0;
        for (int idir = 0;idir < SpaceDim;++idir)
          {
            anisBd += a_normal[idir]*a_normal[idir];
          }
        anisBd = sqrt(anisBd);
        if (anisBd !=0.0)
          {
            a_normal /= anisBd;
          }

        // compute bndryArea and bndryCentroid
        a_bndryArea = Face.getBdLength();
        if (a_bndryArea <= 0.0)
          {
            a_bndryCentroid = RealVect::Zero;
            a_bndryArea = 0.0;
          }
        else
          {
            for (int idir = 0;idir < SpaceDim; ++idir)
              {
                a_bndryCentroid[idir] = answer0[SpaceDim-idir]/a_bndryArea;
              }
            a_bndryArea *= anisBd;
            a_bndryArea /= maxDx;
          }

        for (int edgeNormal = 0; edgeNormal < SpaceDim; ++edgeNormal)
          {
            // loside
            bool coveredLo = edges[edgeNormal*2].isCovered();
            IntVect loiv = a_iv;
            loiv[edgeNormal] -= 1;
            if(a_domain.contains(loiv))
              {
                coveredLo= coveredLo || (a_regIrregCovered(loiv) < 0);
              }
            if (coveredLo)
              {
                a_loArc[edgeNormal].resize(0);
                a_loFaceCentroid[edgeNormal].resize(0);
                a_loAreaFrac[edgeNormal].resize(0);
              }
            else if (!coveredLo)
              {
                a_loArc[edgeNormal].resize(1);
                a_loFaceCentroid[edgeNormal].resize(1);
                a_loAreaFrac[edgeNormal].resize(1);
                IntVect otherIV = a_iv;
                otherIV[edgeNormal] -= 1;
                if (a_domain.contains(otherIV))
                  {
                    //stuff that uses this does not care whether the arcs go
                    //to a regular or irregular cell--I am taking away that distinction
                    a_loArc[edgeNormal][0]= 0;
                  }
                else
                  {
                    // boundary arcs always -1
                    a_loArc[edgeNormal][0] = -1;
                  }
                a_loFaceCentroid[edgeNormal][0] = edges[edgeNormal*2].getEdgeCentroid();
                a_loAreaFrac[edgeNormal][0] = edges[edgeNormal*2].getEdgeLength();
              }

            // hiside
            bool coveredHi = edges[edgeNormal*2+1].isCovered();
            IntVect hiiv = a_iv;
            hiiv[edgeNormal] += 1;
            if(a_domain.contains(hiiv))
              {
                coveredHi = coveredHi || (a_regIrregCovered(hiiv, 0) < 0);
              }
            if (coveredHi)
              {
                a_hiArc[edgeNormal].resize(0);
                a_hiFaceCentroid[edgeNormal].resize(0);
                a_hiAreaFrac[edgeNormal].resize(0);
              }
            else if (!coveredHi)
              {
                a_hiArc[edgeNormal].resize(1);
                a_hiFaceCentroid[edgeNormal].resize(1);
                a_hiAreaFrac[edgeNormal].resize(1);
                IntVect otherIV = a_iv;
                otherIV[edgeNormal] += 1;
                if (a_domain.contains(otherIV))
                  {
                    //stuff that uses this does not care whether the arcs go
                    //to a regular or irregular cell--I am taking away that distinction.
                    //It only ever made sense in the context of a geometry gneration package that could
                    //generate multi-valued cells
                    a_hiArc[edgeNormal][0] = 0;
                  }
                else
                  {
                    // boundary arcs always -1
                    a_hiArc[edgeNormal][0] = -1;
                  }
                a_hiFaceCentroid[edgeNormal][0] = edges[edgeNormal*2+1].getEdgeCentroid();
                a_hiAreaFrac[edgeNormal][0] = edges[edgeNormal*2+1].getEdgeLength();

              }

          }
      }

    if (SpaceDim==3)
      {
        // 1) using the intvect, build up the classes in Moments: edgeMO,
        //    faceMo and finally vofMo
        // 2) check for covered or regular faces
        // 3) call  momentCalc3D
        // 4) keep track of what the output means and fill the variables
        //    requested

        faceMo Faces[6];
        int index = -1;

        for (int faceNormal = 0;faceNormal < SpaceDim;++faceNormal)
          {
            for (int hiLoFace = 0;hiLoFace < 2;++hiLoFace)
              {
                index += 1;
                edgeMo edges[4];
                bool faceCovered;
                bool faceRegular;
                bool faceDontKnow;
                edgeData3D(edges,
                           faceCovered,
                           faceRegular,
                           faceDontKnow,
                           hiLoFace,
                           faceNormal,
                           a_dx,
                           a_iv,
                           a_domain,
                           a_origin);

                assert(faceRegular || faceCovered || faceDontKnow);
                assert((!(faceRegular && faceCovered)) && (!(faceRegular && faceDontKnow)) && (!(faceDontKnow && faceCovered)));
                Faces[index].define(edges,faceNormal,faceCovered,faceRegular,faceDontKnow);

                // if the face is covered we will deal with it later
                if (!Faces[index].isCovered())
                  {
                    Moments geom;
                    // answer0 and answer1 have all the geometric facts
                    std::vector<Real> answer0;
                    std::vector<Real> answer1;

                    int order = 0;
                    answer0 = geom.momentCalc2D(order,Faces[index]);

                    // area of this face
                    Real area=answer0[0];

                    order = 1;
                    answer1 = geom.momentCalc2D(order,Faces[index]);

                    // first we get the centroid of the face
                    RealVect faceCentroid;
                    for (int idir = 0; idir<SpaceDim;++idir)
                      {
                        faceCentroid[idir] = answer1[SpaceDim-1-idir];
                      }

                    if (area > 0.0)
                      {
                        faceCentroid /= area;
                      }
                    else
                      {
                        faceCentroid = RealVect::Zero;
                      }

                    // record these facts in member data
                    Faces[index].setFaceCentroid(faceCentroid);
                    Faces[index].setFaceArea(area);
                  }

                else if (Faces[index].isCovered())
                  {
                    RealVect faceCentroid = RealVect::Zero;
                    Faces[index].setFaceCentroid(faceCentroid);
                    Real area = 0.0;
                    Faces[index].setFaceArea(area);
                  }
              }
          }
#if 1
        // iterate over faces recalculating face area
        // face order is xLo,xHi,yLo,yHi,zLo,zHi
        for (int iFace = 0; iFace < 2*SpaceDim; ++iFace)
          {
            // recalculate face area
            faceMo& face = Faces[iFace];

            // collect exactly two irregular edges or mayday.
            std::vector<RealVect> crossingPt;

            std::vector<int> cPtHiLo;
            std::vector<int> edgeHiLo;
            std::vector<int> edgeDir;

            if (!(face.isCovered()) && !(face.isRegular()))
              {
                for ( int iEdge = 0; iEdge < 4; ++iEdge)
                  {
                    const edgeMo& curEdge = face.retrieveEdge(iEdge);

                    if (curEdge.dontKnow())
                      {
                        // loPt of edge
                        RealVect loPt = curEdge.getLo();

                        // hiPt of edge
                        RealVect hiPt = curEdge.getHi();

                        // direction that varies over edge
                        int direction = curEdge.direction();
                        bool intersectLo = curEdge.getIntersectLo();

                        // for irregular or regular edges at most one pt away from corner
                        if (intersectLo)
                          {
                            crossingPt.push_back(loPt);
                            cPtHiLo.push_back(-1);

                            int hilo = iEdge % 2;
                            edgeHiLo.push_back(hilo);

                            edgeDir.push_back(direction);
                          }
                        else // forced by dontknow
                          {
                            crossingPt.push_back(hiPt);
                            cPtHiLo.push_back(1);

                            int hilo = iEdge % 2;
                            edgeHiLo.push_back(hilo);

                            edgeDir.push_back(direction);
                          }
                      }
                  }
              }

            if (crossingPt.size() == 2)
              {
                // get midpoint of line connecting intersection points
                RealVect midPt = crossingPt[0];
                midPt += crossingPt[1];
                midPt *= 0.5;

                // faceNormal
                int faceNormal = Faces[iFace].getFaceNormal();

                // directions over which the face varies
                int dir1 = (faceNormal + 1) % SpaceDim;
                int dir2 = (faceNormal + 2) % SpaceDim;

                // find max of (deltaDir1,deltaDir2)
                Real deltaDir1 = std::abs(crossingPt[0][dir1] - crossingPt[1][dir1]);
                Real deltaDir2 = std::abs(crossingPt[0][dir2] - crossingPt[1][dir2]);

                // maxDir will be the direction of integration (independent variable)
                // minDir will the direction in which the integrand varies(dependent variable)
                int maxDir;
                int minDir;
                if (deltaDir1 > deltaDir2)
                  {
                    maxDir = dir1;
                    minDir = dir2;
                  }
                else
                  {
                    maxDir = dir2;
                    minDir = dir1;
                  }
                // flip area?
                bool complementArea;

                assert (cPtHiLo.size() == 2);

                // loEdge-loEdge
                if (edgeHiLo[0] == 0 && edgeHiLo[1] == 0)
                  {
                    // both crossingPts must be Hi or both must be Lo
                    assert((cPtHiLo[0] == 1 && cPtHiLo[1] == 1) ||
                           (cPtHiLo[0] == -1 && cPtHiLo[1] == -1));

                    // prismArea gives triangle

                    // if the cPtHiLo[0]= hiPt then one wants triangle
                    if (cPtHiLo[0] == 1)
                      {
                        complementArea = false;
                      }
                    else
                      {
                        complementArea = true;
                      }
                  }

                // hiEdge-hiEdge
                else if (edgeHiLo[0] == 1 && edgeHiLo[1] == 1)
                  {
                    // both crossingPts must be Hi or both must be Lo
                    assert((cPtHiLo[0] == 1 && cPtHiLo[1] == 1) ||
                           (cPtHiLo[0] == -1 && cPtHiLo[1] == -1));
                    // prismArea gives the trapezoid

                    // cPtHiLo[0] == Lo => one wants the triangle area
                    if (cPtHiLo[0] == -1)
                      {
                        complementArea = true;
                      }
                    else
                      {
                        complementArea = false;
                      }
                  }

                // hiEdge-loEdge
                else if (edgeHiLo[0] == 1 && edgeHiLo[1] == 0)
                  {
                    // cpPtHiLo must be the same or the opposite of edgeHiLo
                    assert((cPtHiLo[0] == 1 && cPtHiLo[1] == -1) ||
                           (cPtHiLo[0] == -1 && cPtHiLo[1] == 1));
                    // maxDir > minDir =>prismArea gives trapezoid
                    // maxDir < minDir =>prismArea gives triangle

                    // (cPtHiLo[1] == 1) => one wants trapezoid
                    if (cPtHiLo[1] == 1)
                      {
                        if (maxDir < minDir)
                          {
                            complementArea = true;
                          }
                        else
                          {
                            complementArea = false;
                          }
                      }
                    else
                      // (cPtHiLo[1] == -1) => one wants triangle
                      {
                        if (maxDir < minDir)
                          {
                            complementArea = false;
                          }
                        else
                          {
                            complementArea = true;
                          }
                      }
                  }

                // loEdge-hiEdge
                else if (edgeHiLo[0] == 0 && edgeHiLo[1] == 1)
                  {
                    // triangle + triangle complement or two trapezoids comprise this case

                    // two trapezoids
                    if (cPtHiLo[1] == 1 && cPtHiLo[0] == 1)
                      {
                        assert(edgeDir[0] == edgeDir[1]);
                        complementArea = false;
                      }
                    else if (cPtHiLo[1] == -1 && cPtHiLo[0] == -1)
                      {
                        assert(edgeDir[0] == edgeDir[1]);
                        complementArea = true;
                      }
                    // triangle + triangle complement
                    // if the cPtHiLo[0]= loPt then one wants triangle
                    else if (cPtHiLo[0] == -1 && cPtHiLo[1] == 1 )
                      {
                        assert(edgeDir[0] != edgeDir[1]);
                        // if maxDir < minDir prismArea gives trapezoid
                        // if maxDir > minDir prismArea gives triangle
                        if (maxDir < minDir)
                          {
                            complementArea = true;
                          }
                        else
                          {
                            complementArea = false;
                          }
                      }

                    // cPtHiLo[0]= hiPt => one wants trapezoid
                    else if (cPtHiLo[0] == 1 && cPtHiLo[1] == -1)
                      {
                        if (maxDir < minDir)
                          {
                            complementArea = false;
                          }
                        else
                          {
                            complementArea = true;
                          }
                      }
                  }

                else
                  {
                    amrex::Error("cPtDir or mindir or maxDir not set correctly");
                  }

                // segLo is the lo end of segment within face[iEdge] for Brent Rootfinder
                RealVect segLo = midPt;
                segLo[minDir] = -0.5;

                // segHi is the hi end of segment within face[iEdge] for Brent Rootfinder
                RealVect segHi = midPt;
                segHi[minDir] = 0.5;

                // put segLo and segHi in physical coordinates
                RealVect physSegLo;
                RealVect physSegHi;
                RealVect physMidPt;
                for (int idir = 0; idir < SpaceDim; ++idir)
                  {
                    physSegLo[idir] = a_dx*(segLo[idir] + a_iv[idir] + 0.5) + a_origin[idir];
                    physSegHi[idir] = a_dx*(segHi[idir] + a_iv[idir] + 0.5) + a_origin[idir];
                    physMidPt[idir] = a_dx*(midPt[idir] + a_iv[idir] + 0.5) + a_origin[idir];
                  }

                // find upDir
                std::pair<int,Side::LoHiSide> upDir;

                // physIntercept is along the segment[physSegLo,physSegHi]
                // this segment passes through midPt with direction minDir
                Real physIntercept;
                //                bool dropOrder = false;



                Real fLo = m_implicitFunction->value(physSegLo);
                Real fHi = m_implicitFunction->value(physSegHi);

                // This guards against the "root must be bracketed" error
                // by dropping order
                if (fLo*fHi > 0.0)
                  {
                    //                    dropOrder = true;
                  }
                else
                  {
                    physIntercept = BrentRootFinder(physSegLo, physSegHi, minDir);
                  }

                // put physIntercept into relative coordinates
                Real intercept = physIntercept - a_origin[minDir];
                intercept  /= a_dx;
                intercept -= (a_iv[minDir]+0.5);

                // push_back third pt onto crossingPt
                crossingPt.push_back(midPt);
                crossingPt[2][minDir] = intercept;

                // integrate w.r.t xVec using Prismoidal Rule
                RealVect xVec;
                RealVect yVec;

                // the order of (xVec,yVec) will be sorted out in PrismoidalAreaCalc
                xVec[0] = crossingPt[0][maxDir];
                xVec[1] = crossingPt[2][maxDir];
                xVec[2] = crossingPt[1][maxDir];

                yVec[0] = crossingPt[0][minDir];
                yVec[1] = crossingPt[2][minDir];
                yVec[2] = crossingPt[1][minDir];

                // Prismoidal's rule
                Real area = PrismoidalAreaCalc(xVec,yVec);

                // Only use area if it is valid
                if (area >= 0.0 && area <= 1.0)
                  {
                    // assign area to this value or (1 - this value)
                    if (complementArea)
                      {
                        area = 1.0 - area;
                      }

                    Faces[iFace].setFaceArea(area);
                  }
              }
          }
#endif
        // fill in some arguments of computeVofInternals for the faces
        for (int faceNormal = 0;faceNormal < SpaceDim;++faceNormal)
          {
            bool coveredLo = Faces[faceNormal*2].isCovered();
            IntVect loiv = a_iv;
            loiv[faceNormal] -= 1;
            if(a_domain.contains(loiv))
              {
                coveredLo = coveredLo || (a_regIrregCovered(loiv) < 0);
              }
            if (coveredLo) 
              {
                a_loArc[faceNormal].resize(0);
                a_loFaceCentroid[faceNormal].resize(0);
                a_loAreaFrac[faceNormal].resize(0);
              }
            else if (!coveredLo)
              {
                a_loArc[faceNormal].resize(1);
                a_loFaceCentroid[faceNormal].resize(1);
                a_loAreaFrac[faceNormal].resize(1);
                IntVect otherIV = a_iv;
                otherIV[faceNormal] -= 1;

                if (a_domain.contains(otherIV))
                  {
                    //stuff that uses this does not care whether the arcs go
                    //to a regular or irregular cell--I am taking away that distinction.
                    //It only ever made sense in the context of a geometry gneration package that could
                    //generate multi-valued cells
                    a_loArc[faceNormal][0] = 0;
                  }
                else
                  {
                    // boundary arcs always -1
                    a_loArc[faceNormal][0] = -1;
                  }

                a_loFaceCentroid[faceNormal][0] = Faces[faceNormal*2].getFaceCentroid();

                a_loAreaFrac[faceNormal][0] = Faces[faceNormal*2].getFaceArea();

              }
            else
              {
                amrex::Error("is it coveredLo?");
              }
            bool coveredHi = Faces[faceNormal*2+1].isCovered();
            IntVect hiiv = a_iv;
            hiiv[faceNormal] += 1;
            if(a_domain.contains(hiiv))
              {
                coveredHi = coveredHi ||  (a_regIrregCovered(hiiv, 0) < 0);
              }
            if (coveredHi)
              {
                a_hiArc[faceNormal].resize(0);
                a_hiFaceCentroid[faceNormal].resize(0);
                a_hiAreaFrac[faceNormal].resize(0);
              }
            else if (!coveredHi)
              {
                a_hiArc[faceNormal].resize(1);
                a_hiFaceCentroid[faceNormal].resize(1);
                a_hiAreaFrac[faceNormal].resize(1);
                IntVect otherIV = a_iv;
                otherIV[faceNormal] += 1;
                if (a_domain.contains(otherIV))
                  {
                    //stuff that uses this does not care whether the arcs go
                    //to a regular or irregular cell--I am taking away that distinction.
                    //It only ever made sense in the context of a geometry gneration package that could
                    //generate multi-valued cells
                    a_hiArc[faceNormal][0] = 0;
                  }
                else if (!a_domain.contains(otherIV))
                  {
                    // boundaryArcs always -1
                    a_hiArc[faceNormal][0] = -1;
                  }
                a_hiFaceCentroid[faceNormal][0] = Faces[faceNormal*2+1].getFaceCentroid();
                a_hiAreaFrac[faceNormal][0] = Faces[faceNormal*2+1].getFaceArea();
              }
            else
              {
                amrex::Error("is it coveredHi?");
              }
          }

        // We have enough face data to construct the vof
        vofMo Vof;
        Vof.define(Faces);

        Moments geom;
        int order = 0;
        std::vector<Real> answer0;
        answer0 = geom.momentCalc3D(order,Vof);

        a_volFrac = answer0[0];

        std::vector<Real> answer1;
        order = 1;
        answer1 = geom.momentCalc3D(order,Vof);

        for (int idir=0; idir<SpaceDim;++idir)
          {
            a_volCentroid[idir] = answer1[SpaceDim-1-idir];
          }

        if (a_volFrac == 0.0)
          {
            a_volCentroid = RealVect::Zero;
          }
        else
          {
            a_volCentroid /= a_volFrac;

          }

        Real normalVec[SpaceDim];
        Vof.getNormal(normalVec);
        for (int idir = 0;idir < SpaceDim;++idir)
          {
            a_normal[idir] = normalVec[idir];
          }

        for (int idir = 0;idir < SpaceDim;++idir)
          {
            // (nx,ny,nz)->(nxdydz,nydxdz,nzdxdy)
            a_normal[idir] = normalVec[idir]*a_dx*a_dx;
          }
        Real anisBd = 0.0;
        for (int idir = 0;idir < SpaceDim;++idir)
          {
            anisBd += a_normal[idir]*a_normal[idir];
          }
        anisBd = sqrt(anisBd);
        if (anisBd !=0.0)
          {
            a_normal /= anisBd;
          }

        a_bndryArea = Vof.getBdArea();
        if (a_bndryArea > 0.0)
          {
            for (int idir = 0;idir < SpaceDim;++idir)
              {
                a_bndryCentroid[idir] = answer0[SpaceDim-idir]/a_bndryArea;
              }
            a_bndryArea *= anisBd;
            a_bndryArea /= (maxDx*maxDx);
          }
        else
          {
            a_bndryCentroid = RealVect::Zero;
            a_bndryArea = 0.0;
          }
      }

    // clipping
    // only report adjustments when discrepancy is above the threshold
    //    bool thisVofClipped = false;
    Real discrepancy = 0.0;
    Real volDiscrepancy = 0.0;
    // volFrac out of bounds
    if (a_volFrac < 0.0)
      {
        volDiscrepancy = std::abs(a_volFrac);
        char message[1024];
        if (SpaceDim ==2)
          {
            sprintf(message,"vol fraction (%e) out of bounds. Clipping: (%d,%d)",
                    a_volFrac,a_iv[0],a_iv[1]);
          }
        else if (SpaceDim == 3)
          {
            sprintf(message,"vol frac (%e) out of bounds. Clipping: (%d,%d,%d)",
                    a_volFrac,a_iv[0],a_iv[1],a_iv[2]);
          }
        else
          {
            sprintf(message,"SpaceDim not 2 or 3");
          }

        if (volDiscrepancy > m_threshold)
          {
            std::cout << std::string(message) << std::endl;
          }
        // do the clipping
        //thisVofClipped = true;
        a_volFrac = 0.0;
      }

    if (a_volFrac > 1.0)
      {
        volDiscrepancy = std::abs(1.0 - a_volFrac);
        char message[1024];
        if (SpaceDim ==2)
          {
            sprintf(message,"vol fraction (%e) out of bounds. Clipping: (%d,%d)",
                    a_volFrac,a_iv[0],a_iv[1]);
          }
        else if (SpaceDim == 3)
          {
            sprintf(message,"vol frac (%e) out of bounds. Clipping: (%d,%d,%d)",
                    a_volFrac,a_iv[0],a_iv[1],a_iv[2]);
          }
        else
          {
            sprintf(message,"SpaceDim not 2 or 3");
          }
        if (volDiscrepancy>m_threshold)
          {
            std::cout << message << std::endl;
          }
        // do the clipping
        //thisVofClipped = true;
        a_volFrac = 1.0;
      }

    // area frac out of bounds
    for (int idir = 0; idir<SpaceDim; ++idir)
      {
        for (int num = 0; num < a_loAreaFrac[idir].size();num ++)
          {
            // lo frac too high
            if (a_loAreaFrac[idir][num] > 1.0)
              {
                discrepancy = std::abs(1 - a_loAreaFrac[idir][num]);
                char message[1024];
                if (SpaceDim ==2)
                  {
                    sprintf(message,"lo area fraction (%e) out of bounds. Clipping: (%d,%d)",
                            a_loAreaFrac[idir][num],a_iv[0],a_iv[1]);
                  }
                else if (SpaceDim == 3)
                  {
                    sprintf(message,"lo area fraction (%e) out of bounds. Clipping: (%d,%d,%d)",
                            a_loAreaFrac[idir][num],a_iv[0],a_iv[1],a_iv[2]);
                  }
                else
                  {
                    sprintf(message,"SpaceDim not 2 or 3");
                  }
                if (discrepancy>m_threshold && volDiscrepancy>m_threshold)
                  {
                    std::cout << message << std::endl;
                  }

                // do the clipping
                //thisVofClipped = true;
                a_loAreaFrac[idir][num] = 1.0;
              }
            // lo frac too low
            if (a_loAreaFrac[idir][num] < 0.0)
              {
                discrepancy = std::abs(a_loAreaFrac[idir][num]);
                char message[1024];
                if (SpaceDim ==2)
                  {
                    sprintf(message,"lo area fraction (%e) out of bounds. Clipping: (%d,%d)",
                            a_loAreaFrac[idir][num],a_iv[0],a_iv[1]);
                  }
                else if (SpaceDim == 3)
                  {
                    sprintf(message,"lo area fraction (%e) out of bounds. Clipping: (%d,%d,%d)",
                            a_loAreaFrac[idir][num],a_iv[0],a_iv[1],a_iv[2]);
                  }
                else
                  {
                    sprintf(message,"SpaceDim not 2 or 3");
                  }
                if (discrepancy>m_threshold && volDiscrepancy>m_threshold)
                  {
                    std::cout << message << std::endl;
                  }

                // do the clipping
                //thisVofClipped = true;
                a_loAreaFrac[idir][num] = 0.0;
              }

          }
      }

    for (int idir = 0; idir<SpaceDim; ++idir)
      {
        for (int num = 0; num < a_hiAreaFrac[idir].size();num ++)
          {
            // hi frac too high
            if (a_hiAreaFrac[idir][num] > 1.0)
              {
                discrepancy = std::abs(1 - a_hiAreaFrac[idir][num]);
                char message[1024];
                if (SpaceDim ==2)
                  {
                    sprintf(message,"hi area fraction (%e) out of bounds. Clipping: (%d,%d)",
                            a_hiAreaFrac[idir][num],a_iv[0],a_iv[1]);
                  }
                else if (SpaceDim == 3)
                  {
                    sprintf(message,"hi area fraction (%e) out of bounds. Clipping: (%d,%d,%d)",
                            a_hiAreaFrac[idir][num],a_iv[0],a_iv[1],a_iv[2]);
                  }
                else
                  {
                    sprintf(message,"SpaceDim not 2 or 3");
                  }
                if (discrepancy>m_threshold && volDiscrepancy>m_threshold)
                  {
                    std::cout << message << std::endl;
                  }

                // do the clipping
                //thisVofClipped = true;
                a_hiAreaFrac[idir][num] = 1.0;
              }
            // hi frac too low
            if (a_hiAreaFrac[idir][num] < 0.0)
              {
                discrepancy = std::abs(a_hiAreaFrac[idir][num]);
                char message[1024];
                if (SpaceDim ==2)
                  {
                    sprintf(message,"hi area fraction (%e) out of bounds. Clipping: (%d,%d)",
                            a_hiAreaFrac[idir][num],a_iv[0],a_iv[1]);
                  }
                else if (SpaceDim == 3)
                  {
                    sprintf(message,"hi area fraction (%e) out of bounds. Clipping: (%d,%d,%d)",
                            a_hiAreaFrac[idir][num],a_iv[0],a_iv[1],a_iv[2]);
                  }
                else
                  {
                    sprintf(message,"SpaceDim not 2 or 3");
                  }
                if (discrepancy>m_threshold && volDiscrepancy>m_threshold)
                  {
                    std::cout << message << std::endl;
                  }

                // do the clipping
                //thisVofClipped = true;
                a_hiAreaFrac[idir][num] = 0.0;
              }

          }

      }

    // bndry area out of bounds
    if (a_bndryArea < 0.0)
      {
        discrepancy = std::abs(a_bndryArea);
        char message[1024];
        if (SpaceDim ==2)
          {
            sprintf(message,"boundary area fraction (%e) out of bounds. Clipping: (%d,%d)",
                    a_bndryArea,a_iv[0],a_iv[1]);
          }
        else if (SpaceDim == 3)
          {
            sprintf(message,"boundary area fraction (%e) out of bounds. Clipping: (%d,%d,%d)",
                    a_bndryArea,a_iv[0],a_iv[1],a_iv[2]);
          }
        else
          {
            sprintf(message,"SpaceDim not 2 or 3");
          }
        if (discrepancy>m_threshold && volDiscrepancy>m_threshold)
          {
            std::cout << message << std::endl;
          }

        // do the clipping
        //thisVofClipped = true;
        a_bndryArea = 0.0;
      }

    if (a_bndryArea > sqrt(2.0))
      {
        discrepancy = std::abs(sqrt(2.0) - a_bndryArea);
        char message[1024];
        if (SpaceDim ==2)
          {
            sprintf(message,"boundary area fraction (%e) out of bounds. Clipping: (%d,%d)",
                    a_bndryArea,a_iv[0],a_iv[1]);
          }
        else if (SpaceDim == 3)
          {
            sprintf(message,"boundary area fraction (%e) out of bounds. Clipping: (%d,%d,%d)",
                    a_bndryArea,a_iv[0],a_iv[1],a_iv[2]);
          }
        else
          {
            sprintf(message,"SpaceDim not 2 or 3");
          }
        if (discrepancy>m_threshold && volDiscrepancy>m_threshold)
          {
            std::cout << message << std::endl;
          }
        // do the clipping
        //thisVofClipped = true;
        a_bndryArea = sqrt(2.0);
      }

    // volCentroid out of bounds
    for (int idir =0;idir<SpaceDim;++idir)
      {
        if (a_volCentroid[idir] > 0.5)
          {
            discrepancy = std::abs(0.5 - a_volCentroid[idir]);
            char message[1024];
            if (SpaceDim ==2)
              {
                sprintf(message,"volCentroid (%e) out of bounds. Clipping: (%d,%d)",
                        a_volCentroid[idir],a_iv[0],a_iv[1]);
              }
            else if (SpaceDim == 3)
              {
                sprintf(message,"volCentroid(%e) out of bounds. Clipping: (%d,%d,%d)",
                        a_volCentroid[idir],a_iv[0],a_iv[1],a_iv[2]);
              }
            else
              {
                sprintf(message,"SpaceDim not 2 or 3");
              }
            if (discrepancy>m_threshold && volDiscrepancy>m_threshold)
              {
                std::cout << message << std::endl;

              }
            // do the clipping
            //thisVofClipped = true;
            a_volCentroid[idir] = 0.5;
          }
        if (a_volCentroid[idir] < -0.5)
          {
            discrepancy = std::abs(-0.5 - a_volCentroid[idir]);
            char message[1024];
            if (SpaceDim ==2)
              {
                sprintf(message,"volCentroid (%e) out of bounds. Clipping: (%d,%d)",
                        a_volCentroid[idir],a_iv[0],a_iv[1]);
              }
            else if (SpaceDim == 3)
              {
                sprintf(message,"volCentroid (%e) out of bounds. Clipping: (%d,%d,%d)",
                        a_volCentroid[idir],a_iv[0],a_iv[1],a_iv[2]);
              }
            else
              {
                sprintf(message,"SpaceDim not 2 or 3");
              }
            if (discrepancy > m_threshold&& volDiscrepancy>m_threshold)
              {
                std::cout << message << std::endl;
              }
            // do the clipping
            //thisVofClipped = true;
            a_volCentroid[idir] = -0.5;
          }

        // boundary centroid out of bounds
        if (a_bndryCentroid[idir] > 0.5)
          {
            discrepancy = std::abs(0.5 - a_bndryCentroid[idir]);
            char message[1024];
            if (SpaceDim ==2)
              {
                sprintf(message,"bndryCentroid (%e) out of bounds. Clipping: (%d,%d)",
                        a_bndryCentroid[idir],a_iv[0],a_iv[1]);
              }
            else if (SpaceDim == 3)
              {
                sprintf(message,"boundary Centroid (%e) out of bounds. Clipping: (%d,%d,%d)",
                        a_bndryCentroid[idir],a_iv[0],a_iv[1],a_iv[2]);
              }
            else
              {
                sprintf(message,"SpaceDim not 2 or 3");
              }
            if (discrepancy>m_threshold && volDiscrepancy>m_threshold)
              {
                std::cout << message << std::endl;
              }
            // do the clipping
            //thisVofClipped = true;
            a_bndryCentroid[idir] = 0.5;
          }

        if (a_bndryCentroid[idir] < -0.5)
          {
            discrepancy = std::abs(-0.5 - a_bndryCentroid[idir]);
            char message[1024];
            if (SpaceDim ==2)
              {
                sprintf(message,"bndryCentroid (%e) out of bounds. Clipping: (%d,%d)",
                        a_bndryCentroid[idir],a_iv[0],a_iv[1]);
              }
            else if (SpaceDim == 3)
              {
                sprintf(message,"bndryCentroid (%e) out of bounds. Clipping: (%d,%d,%d)",
                        a_bndryCentroid[idir],a_iv[0],a_iv[1],a_iv[2]);
              }
            else
              {
                sprintf(message,"SpaceDim not 2 or 3");
              }
            if (discrepancy > m_threshold && volDiscrepancy>m_threshold)
              {
                std::cout << message << std::endl;
              }
            // do the clipping
            //thisVofClipped = true;
            a_bndryCentroid[idir] = -0.5;
          }
      }
    // loFaceCentroid out of bounds
    for (int idir = 0; idir<SpaceDim; ++idir)
      {
        for (int num = 0; num < a_loFaceCentroid[idir].size();num ++)
          {
            for (int jdir = 0; jdir<SpaceDim; ++jdir)
              {
                if (a_loFaceCentroid[idir][num][jdir] > 0.5)
                  {
                    discrepancy = std::abs(0.5 - a_loFaceCentroid[idir][num][jdir]);
                    char message[1024];
                    if (SpaceDim ==2)
                      {
                        sprintf(message,"loFaceCentroid (%e) out of bounds. Clipping: (%d,%d)",
                                a_loFaceCentroid[idir][num][jdir],a_iv[0],a_iv[1]);
                      }
                    else if (SpaceDim == 3)
                      {
                        sprintf(message,"loFaceCentroid (%e) out of bounds. Clipping: (%d,%d,%d)",
                                a_loFaceCentroid[idir][num][jdir],a_iv[0],a_iv[1],a_iv[2]);
                      }
                    else
                      {
                        sprintf(message,"SpaceDim not 2 or 3");
                      }
                    if (discrepancy > m_threshold && volDiscrepancy>m_threshold)
                      {
                        std::cout << message << std::endl;

                      }
                    // do the clipping
                    //thisVofClipped = true;
                    a_loFaceCentroid[idir][num][jdir] = 0.5;
                  }
                if (a_loFaceCentroid[idir][num][jdir] < -0.5)
                  {
                    discrepancy = std::abs(-0.5 - a_loFaceCentroid[idir][num][jdir]);
                    char message[1024];
                    if (SpaceDim ==2)
                      {
                        sprintf(message,"loFaceCentroid (%e) out of bounds. Clipping: (%d,%d)",
                                a_loFaceCentroid[idir][num][jdir],a_iv[0],a_iv[1]);
                      }
                    else if (SpaceDim == 3)
                      {
                        sprintf(message,"loFaceCentroid (%e) out of bounds. Clipping: (%d,%d,%d)",
                                a_loFaceCentroid[idir][num][jdir],a_iv[0],a_iv[1],a_iv[2]);
                      }
                    else
                      {
                        sprintf(message,"SpaceDim not 2 or 3");
                      }
                    if (discrepancy > m_threshold && volDiscrepancy>m_threshold)
                      {
                        std::cout << message << std::endl;

                      }
                    // do the clipping
                    //thisVofClipped = true;
                    a_loFaceCentroid[idir][num][jdir] = -0.5;
                  }
              }
          }
      }
    // hiFaceCentroid out of bounds
    for (int idir = 0; idir<SpaceDim; ++idir)
      {
        for (int num = 0; num < a_hiFaceCentroid[idir].size();num ++)
          {
            for (int jdir = 0; jdir<SpaceDim; ++jdir)
              {
                if (a_hiFaceCentroid[idir][num][jdir] > 0.5)
                  {
                    discrepancy = std::abs(0.5 - a_hiFaceCentroid[idir][num][jdir]);
                    char message[1024];
                    if (SpaceDim ==2)
                      {
                        sprintf(message,"hiFaceCentroid (%e) out of bounds. Clipping: (%d,%d)",
                                a_hiFaceCentroid[idir][num][jdir],a_iv[0],a_iv[1]);
                      }
                    else if (SpaceDim == 3)
                      {
                        sprintf(message,"hiFaceCentroid (%e) out of bounds. Clipping: (%d,%d,%d)",
                                a_hiFaceCentroid[idir][num][jdir],a_iv[0],a_iv[1],a_iv[2]);
                      }
                    else
                      {
                        sprintf(message,"SpaceDim not 2 or 3");
                      }
                    if (discrepancy > m_threshold && volDiscrepancy>m_threshold)
                      {
                        std::cout << message << std::endl;
                      }
                    // do the clipping
                    //thisVofClipped = true;
                    a_hiFaceCentroid[idir][num][jdir] = 0.5;
                  }
                if (a_hiFaceCentroid[idir][num][jdir] < -0.5)
                  {
                    discrepancy = std::abs(0.5 - a_hiFaceCentroid[idir][num][jdir]);
                    char message[1024];
                    if (SpaceDim ==2)
                      {
                        sprintf(message,"hiFaceCentroid (%e) out of bounds. Clipping: (%d,%d)",
                                a_hiFaceCentroid[idir][num][jdir],a_iv[0],a_iv[1]);
                      }
                    else if (SpaceDim == 3)
                      {
                        sprintf(message,"hiFaceCentroid (%e) out of bounds. Clipping: (%d,%d,%d)",
                                a_hiFaceCentroid[idir][num][jdir],a_iv[0],a_iv[1],a_iv[2]);
                      }
                    else
                      {
                        sprintf(message,"SpaceDim not 2 or 3");
                      }
                    if (discrepancy > m_threshold && volDiscrepancy > m_threshold)
                      {
                        std::cout << message << std::endl;
                      }
                    // do the clipping
                    //thisVofClipped = true;
                    a_hiFaceCentroid[idir][num][jdir] = -0.5;
                  }
              }
          }
      }

  }


  void GeometryShop::edgeData3D(edgeMo a_edges[4],
                                bool& a_faceCovered,
                                bool& a_faceRegular,
                                bool& a_faceDontKnow,
                                const int a_hiLoFace,
                                const int a_faceNormal,
                                const Real& a_dx,
                                const IntVect& a_iv,
                                const Box& a_domain,
                                const RealVect& a_origin) const
  {
    a_faceRegular = true;
    a_faceCovered = true;
    a_faceDontKnow = false;

    int index = -1;

    // edge order is lexigraphic xLo,xHi,yLo,yHi
    for (int dom = 0; dom < 3; ++dom)
      {
        if (dom != a_faceNormal)
          {
            for (int lohi = 0; lohi < 2; ++lohi)
              {
                // which edge 0,1,2, or 3 in lexigraphic order is given by index
                index += 1;
                // range is the direction along which the edge varies
                int range = 3 - a_faceNormal - dom;

                RealVect LoPt;
                bool LoPtChanged = false;
                Real funcLo;

                RealVect HiPt;
                bool HiPtChanged = false;
                Real funcHi;

                // put LoPt in physical coordinates
                LoPt[a_faceNormal] = a_origin[a_faceNormal]+
                  (a_iv[a_faceNormal]+a_hiLoFace)*a_dx;
                LoPt[dom] = a_origin[dom] + (a_iv[dom]+lohi)*a_dx;
                LoPt[range] = a_origin[range] + (a_iv[range])*a_dx;

                // put HiPt in physical coordinates
                HiPt[a_faceNormal] = a_origin[a_faceNormal] +
                  (a_iv[a_faceNormal]+a_hiLoFace)*a_dx;
                HiPt[dom] = a_origin[dom] + (a_iv[dom]+lohi)*a_dx;
                HiPt[range] = a_origin[range] + (a_iv[range]+1)*a_dx;

                // find the midpoint
                RealVect MidPt = LoPt;
                MidPt += HiPt;
                MidPt /= 2.0;

                Real signHi;
                Real signLo;

                // placeholders for edgeType
                bool covered  = false;
                bool regular  = false;
                bool dontKnow = false;

                //                RealVect interceptPt = RealVect::Zero;

                funcHi = m_implicitFunction->value(HiPt);
                funcLo = m_implicitFunction->value(LoPt);

                // For level set data negative -> in the fluid
                //                    positive -> out of the fluid
                signHi = -funcHi;
                signLo = -funcLo;

                edgeType(covered,regular,dontKnow,signHi,signLo);

                // now we know the boolean values so we can set the edge Hi  and Lo pts
                if (covered)
                  {
                    a_faceRegular=false;

                    LoPt[range] = a_origin[range] + (a_iv[range]+0.5)*a_dx;
                    LoPtChanged = true;

                    HiPt[range] = a_origin[range] + (a_iv[range]+0.5)*a_dx;
                    HiPtChanged = true;
                  }
                else if (regular)
                  {
                    a_faceCovered = false;
                  }
                else if (dontKnow)
                  {
                    a_faceRegular = false;
                    a_faceCovered = false;
                    a_faceDontKnow = true;

                    Real intercept;
                  
                    // find where the surface intersects the edge
                    intercept = BrentRootFinder(LoPt, HiPt, range);

                    if (funcHi >= 0 && funcLo*funcHi <= 0)
                      {
                        HiPt[range] = intercept;
                        HiPtChanged = true;
                      }
                    else if (funcLo >= 0 && funcLo*funcHi <= 0)
                      {
                        LoPt[range] = intercept;
                        LoPtChanged = true;
                      }
                    else
                      {
                        amrex::Error("Bogus intersection calculated");
                      }
                  }

                // put LoPt and HiPt in local coordinates
                if (a_hiLoFace == 0)
                  {
                    LoPt[a_faceNormal] = -0.5;
                    HiPt[a_faceNormal] = -0.5;
                  }
                else
                  {
                    LoPt[a_faceNormal] =  0.5;
                    HiPt[a_faceNormal] =  0.5;
                  }

                if (lohi == 0)
                  {
                    LoPt[dom] = -0.5;
                    HiPt[dom] = -0.5;
                  }
                else
                  {
                    LoPt[dom] =  0.5;
                    HiPt[dom] =  0.5;
                  }

                if (LoPtChanged)
                  {
                    LoPt[range] -= a_origin[range];
                    LoPt[range] /= a_dx;
                    LoPt[range] -= (a_iv[range] + 0.5);
                  }
                else
                  {
                    LoPt[range] = -0.5;
                  }

                if (HiPtChanged)
                  {
                    HiPt[range] -= a_origin[range];
                    HiPt[range] /= a_dx;
                    HiPt[range] -= (a_iv[range] + 0.5);
                  }
                else
                  {
                    HiPt[range] =  0.5;
                  }

                assert((!(regular && covered)) && (!(regular && dontKnow)) && (!(dontKnow && covered)));
                assert(regular || covered || (!(LoPtChanged && HiPtChanged)));
                assert(regular || covered || dontKnow);
                assert(regular || covered || LoPtChanged || HiPtChanged);
                bool intersectLo = LoPtChanged;
                edgeMo Edge;
                // range means the coordinate direction that varies over the length of the edge
                Edge.define(LoPt,HiPt,intersectLo, range,covered,regular,dontKnow);
                a_edges[index] = Edge;
              }
          }
      }
    return;
  }
  void GeometryShop::edgeData2D(edgeMo a_edges[4],
                                bool& a_faceCovered,
                                bool& a_faceRegular,
                                bool& a_faceDontKnow,
                                const Real& a_dx,
                                const IntVect& a_iv,
                                const Box& a_domain,
                                const RealVect& a_origin) const
  {
    // index counts which edge:xLo=0,xHi=1,yLo=2,yHi=3
    int index = -1;

    a_faceRegular = true;
    a_faceCovered = true;
    a_faceDontKnow = false;

    // domain means the direction normal to the edge
    for (int domain = 0; domain < 2;++domain)
      {
        for (int lohi = 0; lohi < 2;++lohi)
          {
            index += 1;

            // range is the direction along the edge
            int range = 1-domain;

            // Express HiPt. LoPt and MidPt in physical coordinates
            RealVect LoPt;
            bool LoPtChanged = false;
            LoPt[domain] = a_origin[domain] + (a_iv[domain]+lohi)*a_dx;
            LoPt[range]  = a_origin[range]  + (a_iv[range])*a_dx;

            RealVect HiPt;
            bool HiPtChanged = false;
            HiPt[domain] = a_origin[domain] + (a_iv[domain]+lohi)*a_dx;
            HiPt[range] =  a_origin[range] + (a_iv[range]+1)*a_dx;

            RealVect MidPt = HiPt;
            MidPt += LoPt;
            MidPt /= 2.0;

            // decide which type of edge
            bool covered;
            bool regular;
            bool dontKnow;

            // function value
            Real funcHi = m_implicitFunction->value(HiPt);
            Real funcLo = m_implicitFunction->value(LoPt);

            // the sign of signHi and signLo determine edgetype
            Real signHi;
            Real signLo;

            // For level set data negative -> in the fluid
            //                    positive -> out of the fluid
            signHi = -funcHi;
            signLo = -funcLo;

            edgeType(covered,regular,dontKnow,signHi,signLo);

            // Given edgeType, set the edge Hi  and Lo pts
            if (covered)
              {
                a_faceRegular=false;

                LoPt[range] = a_origin[range] + (a_iv[range]+0.5)*a_dx;
                LoPtChanged = true;

                HiPt[range] = a_origin[range] + (a_iv[range]+0.5)*a_dx;
                HiPtChanged = true;
              }
            else if (regular)
              {
                a_faceCovered = false;
              }
            else if (dontKnow)
              {
                a_faceRegular  = false;
                a_faceCovered  = false;
                a_faceDontKnow = true;

                // find where the surface intersects the edge
                Real intercept;

                intercept = BrentRootFinder(LoPt, HiPt, range);

                // choose the midpoint for an ill-conditioned problem
                if (intercept<LoPt[range] || intercept>HiPt[range])
                  {
                    std::cout<<"GeometryShop::edgeData: Ill-conditioned edge data"<<std::endl;
                    intercept = (LoPt[range]+HiPt[range])/2.0;
                  }

                if (funcHi >= 0 && funcLo*funcHi <= 0)
                  {
                    HiPt[range] = intercept;
                    HiPtChanged = true;
                  }
                else if (funcLo >= 0 && funcLo*funcHi <= 0)
                  {
                    LoPt[range] = intercept;
                    LoPtChanged = true;
                  }
                else
                  {
                    amrex::Error("Bogus intersection calculated");
                  }
              }

            // express the answer relative to dx and cell-center
            if (lohi == 0)
              {
                LoPt[domain] = -0.5;
                HiPt[domain] = -0.5;
              }
            else
              {
                LoPt[domain] =  0.5;
                HiPt[domain] =  0.5;
              }

            if (LoPtChanged)
              {
                LoPt[range] -= a_origin[range];
                LoPt[range] /= a_dx;
                LoPt[range] -= (a_iv[range] + 0.5);
              }
            else
              {
                LoPt[range] = -0.5;
              }

            if (HiPtChanged)
              {
                HiPt[range] -= a_origin[range];
                HiPt[range] /= a_dx;
                HiPt[range] -= (a_iv[range] + 0.5);
              }
            else
              {
                HiPt[range] =  0.5;
              }

            assert(regular || covered || (!(LoPtChanged && HiPtChanged)));
            assert(regular || covered || dontKnow);
            assert((!(regular && covered)) && (!(regular && dontKnow)) && (!(dontKnow && covered)));
            assert(regular || covered || LoPtChanged || HiPtChanged);
            // default is something invalid
            bool intersectLo = LoPtChanged;

            // define this edge
            // Note we have some irregular edges of 0 length
            a_edges[index].define(LoPt,HiPt,intersectLo, range,covered,regular,dontKnow);
          }
      }
  }

  void GeometryShop::edgeType(bool& a_covered,
                              bool& a_regular,
                              bool& a_dontKnow,
                              Real& a_signHi,
                              Real& a_signLo) const
  {
    // if signHi and signLo are both positive
    if (a_signHi > 0.0 && a_signLo > 0.0)
      {
        a_covered  = false;
        a_regular  = true;
        a_dontKnow = false;
      }

    // if signHi and signLo are both negative
    else if (a_signHi <= 0.0 && a_signLo <= 0.0)
      {
        a_covered  = true;
        a_regular  = false;
        a_dontKnow = false;
      }

    // if signHi or signLo both are zero
    else if (a_signHi == 0.0 && a_signLo == 0.0)
      {
        a_covered  = true;
        a_regular  = false;
        a_dontKnow = false;
      }

    // otherwise signLo*signHi <= 0
    // in this case we will look for an intersection point
    else
      {
        a_covered  = false;
        a_regular  = false;
        a_dontKnow = true;
      }

    return;
  }

  Real GeometryShop::Min(const Real x, const Real y)const
  {
    Real retval;
    if (x < y)
      {
        retval = x;
      }
    else
      {
        retval = y;
      }
    return retval;
  }

  //  The following is an implementation of "Brent's Method"
  //    for one-dimensional root finding. Pseudo-code for this
  //    algorithm can be found on p. 253 of "Numerical Recipes"
  //    ISBN 0-521-30811-9
  Real GeometryShop::BrentRootFinder(const RealVect& a_x1,
                                     const RealVect& a_x2,
                                     const int&      a_range) const
  {
    const Real tol = 1.0e-12;

    //  Max allowed iterations and floating point precision
    const unsigned int  MAXITER = 100;
    const Real      EPS   = 3.0e-15;

    unsigned int i;
    RealVect aPt;
    RealVect bPt;
    Real c, fa, fb, fc;
    Real d, e;
    Real tol1, xm;
    Real p, q, r, s;

    aPt = a_x1;
    bPt = a_x2;

    fa = -m_implicitFunction->value(aPt);
    fb = -m_implicitFunction->value(bPt);

    //  Init these to be safe
    c = d = e = 0.0;

    if (fb*fa > 0)
      {
        std::cout << "fa " << fa << " fb " << fb << std::endl;
        amrex::Error("GeometryShop::BrentRootFinder. Root must be bracketed, but instead the supplied end points have the same sign.");
      }

    fc = fb;

    for (i = 0; i < MAXITER; i++)
      {
        if (fb*fc > 0)
          {
            //  Rename a, b, c and adjust bounding interval d
            c = aPt[a_range];
            fc  = fa;
            d = bPt[a_range] - aPt[a_range];
            e = d;
          }

        if (std::abs(fc) < std::abs(fb))
          {
            aPt[a_range] = bPt[a_range];
            bPt[a_range] = c;
            c = aPt[a_range];
            fa  = fb;
            fb  = fc;
            fc  = fa;
          }

        //  Convergence check
        tol1  = 2.0 * EPS * std::abs(bPt[a_range]) + 0.5 * tol;
        xm    = 0.5 * (c - bPt[a_range]);

        if (std::abs(xm) <= tol1 || fb == 0.0)
          {
            break;
          }

        if (std::abs(e) >= tol1 && std::abs(fa) > std::abs(fb))
          {
            //  Attempt inverse quadratic interpolation
            s = fb / fa;
            if (aPt[a_range] == c)
              {
                p = 2.0 * xm * s;
                q = 1.0 - s;
              }
            else
              {
                q = fa / fc;
                r = fb / fc;
                p = s * (2.0 * xm * q * (q-r) - (bPt[a_range]-aPt[a_range]) * (r-1.0));
                q = (q-1.0) * (r-1.0) * (s-1.0);
              }

            //  Check whether in bounds
            if (p > 0) q = -q;

            p = std::abs(p);

            if (2.0 * p < Min(3.0*xm*q-std::abs(tol1*q), std::abs(e*q)))
              {
                //  Accept interpolation
                e = d;
                d = p / q;
              }
            else
              {
                //  Interpolation failed, use bisection
                d = xm;
                e = d;
              }
          }
        else
          {
            //  Bounds decreasing too slowly, use bisection
            d = xm;
            e = d;
          }

        //  Move last best guess to a
        aPt[a_range] = bPt[a_range];
        fa  = fb;

        //  Evaluate new trial root
        if (std::abs(d) > tol1)
          {
            bPt[a_range] = bPt[a_range] + d;
          }
        else
          {
            if (xm < 0) bPt[a_range] = bPt[a_range] - tol1;
            else        bPt[a_range] = bPt[a_range] + tol1;
          }

        fb = -m_implicitFunction->value(bPt);
      }

    if (i >= MAXITER)
      {
        std::cerr  << "BrentRootFinder: exceeding maximum iterations: "
                   << MAXITER << std::endl;
      }
    //  //  Keep statistics
    //     statCount++;
    //     statSum += i;
    //     statSum2  += i*i;

    return bPt[a_range];
  }

  // Coordinates are in the box: [-0.5,0.5] x [-0.5,0.5]
  // It is assumed that the spacing in x is uniform
  Real GeometryShop::PrismoidalAreaCalc(RealVect& a_xVec,
                                        RealVect& a_yVec) const
  {
    // The area of the parabola determined by the three input points
    Real retval;

    // See if parabolic approximation, a*x^2 + b*x + c, stays in bounds
    Real aScale;  // a times 2h^2
    Real bScale;  // b times 2h
    Real cScale;  // c

    aScale = a_yVec[2] - 2*a_yVec[1] + a_yVec[0];
    bScale = a_yVec[2] - a_yVec[0];
    cScale = a_yVec[1];

    if ((std::abs(bScale) > 2*std::abs(aScale)) ||
        (std::abs(8*aScale*cScale - bScale*bScale) <= std::abs(4*aScale)))
      {
        // Compute the area of the parabola

        // Integrate w.r.t. x, y is a function of x
        // (x_vec[1],y_vec[1]) is the middle point

        // Learn whether x_vec[0] or x_vec[2] is smaller (with origin = cell center)
        Real largeX = a_xVec[2];
        Real smallX = a_xVec[0];

        // Y's that go with the largeX and smallX
        Real largeX_Y = a_yVec[2];
        Real smallX_Y = a_yVec[0];

        // Swap large for small, if necessary
        if (a_xVec[0] > a_xVec[2])
          {
            largeX = a_xVec[0];
            smallX = a_xVec[2];

            largeX_Y = a_yVec[0];
            smallX_Y = a_yVec[2];
          }

        // Find h
        Real h = 0.5 * (largeX - smallX);
        assert (h >= 0.0);

        // Prismoidal rule, which is exact on cubics.
        retval = (h/3.0) * (a_yVec[0] + 4.0 * a_yVec[1] + a_yVec[2]);

        // yVec relative to cell center. Hence add 0.5, but
        // adding 0.5 to each y in the line above adds (h/3) * 3 = h)
        retval += h;

        // Unaccounted for rectangle at the high end of the face.
        retval += (0.5 - largeX) * (largeX_Y + 0.5);

        // Similarly, the unaccounted for rectangle may be at the small end.
        retval += (smallX + 0.5) * (smallX_Y + 0.5);
      }
    else
      {
        // If the extreme of the parabola occurs between a_xVec[0] and a_xVec[2]
        // and the lies outside the current cell then return a negative area so
        // the linear approximation is used
        retval = -1.0;
      }

    return retval;
  }

}//namespace amrex
