      module constants_module
      implicit none
      real*8, parameter :: one_over_eight = 1.0D0/8.0D0, WG=8.0D0
      
      ! ========== TUNING PARAMETERS ==========
      ! Reset to 1.0D0 for new algorithm - old tuning values may not apply
      real*8, parameter :: SCALE_HOURGLASS = 1.0D0   ! Hourglass force scale
      real*8, parameter :: SCALE_K_MATRIX = 1.0D0    ! K matrix scale
      real*8, parameter :: SCALE_GAMMA = 1.0D0       ! Gamma vector scale
      real*8, parameter :: SCALE_C_TILDE = 1.0D0     ! C_tilde scale (now Cmtxh)
      ! =======================================
            
      ! 修正后的沙漏形状向量 h (来自用户提供的表格)
      ! h1 = (1, -1, 1, -1, 1, -1, 1, -1)
      ! h2 = (1, -1, -1, 1, -1, 1, 1, -1)
      ! h3 = (1,  1, -1, -1, -1, -1, 1,  1)
      ! h4 = (-1, 1, -1, 1,  1, -1, 1, -1)
      ! 注意：FORTRAN中数组是列主序，所以我们按列填充
      real*8, parameter :: H_VECTORS(8,4) = reshape([
     * 1.D0, -1.D0,  1.D0, -1.D0,  1.D0, -1.D0,  1.D0, -1.D0,  ! h1
     * 1.D0, -1.D0, -1.D0,  1.D0, -1.D0,  1.D0,  1.D0, -1.D0,  ! h2
     * 1.D0,  1.D0, -1.D0, -1.D0, -1.D0, -1.D0,  1.D0,  1.D0,  ! h3
     * -1.D0,  1.D0, -1.D0,  1.D0,  1.D0, -1.D0,  1.D0, -1.D0   ! h4
     * ],[8,4])

      real*8, parameter :: XiI(8,3) = reshape([
     * -1.D0,1.D0,1.D0,-1.D0,-1.D0,1.D0,1.D0,-1.D0,
     * -1.D0,-1.D0,1.D0,1.D0,-1.D0,-1.D0,1.D0,1.D0,
     * -1.D0,-1.D0,-1.D0,-1.D0,1.D0,1.D0,1.D0,1.D0],[8,3])
            
      end module constants_module

      SUBROUTINE VUEL(nblock,rhs,amass,dtimeStable,svars,nsvars,
     1                energy,
     2                nnode,ndofel,props,nprops,jprops,njprops,
     3                coords,mcrd,u,du,v,a,
     4                jtype,jElem,
     5                time,period,dtimeCur,dtimePrev,kstep,kinc,
     6                lflags,
     7                dMassScaleFactor,
     8                predef,npredef,
     9                jdltyp, adlmag)
C
      use constants_module
      include 'vaba_param.inc'

C     Operation codes
      parameter ( jMassCalc            = 1,
     * jIntForceAndDtStable = 2,
     * jExternForce         = 3)

C     Flag indices
      parameter (iProcedure = 1,
     * iNlgeom    = 2,
     * iOpCode    = 3,
     * nFlags     = 3)

C     Energy array indices
      parameter ( iElPd = 1, iElCd = 2, iElIe = 3, iElTs = 4,
     * iElDd = 5, iElBv = 6, iElDe = 7, iElHe = 8, iUnused = 9,
     * iElTh = 10, iElDmd = 11, iElDc = 12, nElEnergy = 12)

C     Predefined variables indices
      parameter ( iPredValueNew = 1, iPredValueOld = 2, nPred = 2)

C     Time indices
      parameter (iStepTime  = 1, iTotalTime = 2, nTime = 2)

      parameter(ngauss=8)

      dimension rhs(nblock,ndofel),amass(nblock,ndofel,ndofel),
     1          dtimeStable(nblock),
     2          svars(nblock,nsvars),energy(nblock,nElEnergy),
     3          props(nprops),jprops(njprops),
     4          jElem(nblock),time(nTime),lflags(nFlags),
     5          coords(nblock,nnode,mcrd),
     6          u(nblock,ndofel), du(nblock,ndofel),
     7          v(nblock,ndofel), a(nblock, ndofel),
     8          dMassScaleFactor(nblock),  
     9          predef(nblock,nnode,npredef,nPred),
     * adlmag(nblock)

      real*8 DMAT(6,6), FJAC(3,3), FJACINV(3,3), DETJ, COORD(8,3),
     * BiI(8,3), B(6,24), strain(6), dstrain(6), stress(6), 
     * qia(3,4), gammas(8,4), Velocity(8,3), fHG(24),
     * shapes(8),mij,rowMass(ndofel), dudx(3,3),
     * f_hg_old(3,4), df_hg(3,4), f_hg_new(3,4), C_tilde(6,6),
     * dNdzeta(3,8), dsigma_temp(6), E, FNU, rho, dt, VOL,
     * delta_u(24), u_old(24), Kmat(4,4,3,3), K_alpha_alpha(6,6),
     * K_alpha_u(4,6,3), K_alpha_alpha_inv(6,6), mises_stress

      ! ========== C++ MODIFICATION START ==========
      ! 添加初始坐标、雅可比矩阵、行列式和体积比
      real*8 COORD_INITIAL(8,3), FJAC_INITIAL(3,3), 
     &       FJACINV_INITIAL(3,3), DETJ_INITIAL, rj
      ! ========== C++ MODIFICATION END ==========

      ! Gauss point coordinates and weights (for mass matrix)
      real*8 xi(8), eta(8), zeta(8), w(8)
      data xi   / -0.577350269189626D0,  0.577350269189626D0,
     * -0.577350269189626D0,  0.577350269189626D0,
     * -0.577350269189626D0,  0.577350269189626D0,
     * -0.577350269189626D0,  0.577350269189626D0 /
      data eta  / -0.577350269189626D0, -0.577350269189626D0,
     * 0.577350269189626D0,  0.577350269189626D0,
     * -0.577350269189626D0, -0.577350269189626D0,
     * 0.577350269189626D0,  0.577350269189626D0 /
      data zeta / -0.577350269189626D0, -0.577350269189626D0,
     * -0.577350269189626D0, -0.577350269189626D0,
     * 0.577350269189626D0,  0.577350269189626D0,
     * 0.577350269189626D0,  0.577350269189626D0 /
      data w    /  1.0D0, 1.0D0, 1.0D0, 1.0D0, 1.0D0, 1.0D0, 1.0D0, 1.0D0 /

      logical :: firstrun = .true.

      if(firstrun) then
          write(*,*) "========================================================"
          write(*,*) " VUEL C3D8R with Puso EAS Physical Stabilization"
          write(*,*) " Paper: Int. J. Numer. Meth. Engng 2000; 49:1029-1064"
          write(*,*) "========================================================"
          write(*,*) " nblock  =", nblock
          write(*,*) " nnode   =", nnode
          write(*,*) " ndofel  =", ndofel
          write(*,*) " nsvars  =", nsvars
          write(*,*) " nprops  =", nprops
          write(*,*) ""
          write(*,*) " Required state variables: 48"
          write(*,*) " (6 strain + 6 stress + 12 hourglass forces"
          write(*,*) "  + 24 old displacements)"
          if(nsvars .lt. 48) then
              write(*,*) ""
              write(*,*) "ERROR: Insufficient state variables!"
              write(*,*) "Required: 48, Provided:", nsvars
              write(*,*) "Please add to your input file:"
              write(*,*) "*USER OUTPUT VARIABLES"
              write(*,*) "48"
              call exit(1)
          endif
          if(ndofel .ne. 24) then
              write(*,*) ""
              write(*,*) "ERROR: ndofel should be 24 (8 nodes * 3 DOF)"
              write(*,*) "Found:", ndofel
              call exit(1)
          endif
          if(nnode .ne. 8) then
              write(*,*) ""
              write(*,*) "ERROR: nnode should be 8 for C3D8R element"
              write(*,*) "Found:", nnode
              call exit(1)
          endif
      endif

      if(firstrun .and. kstep.eq.1 .and. kinc.eq.1) then
          svars = 0.0D0
          write(*,*) "State variables initialized to zero."
      endif

C     Material properties
      E = props(1)
      FNU = props(2)
      rho = props(3)

      if(lflags(iOpCode).eq.jMassCalc) then
            ! Mass matrix calculation (once only)
            if(firstrun) write(*,*) "Starting mass matrix calculation..."
            do kblock = 1,nblock
                  do I=1,3
                        do J=1,8
                              COORD(J,I)=coords(kblock,J,I)
                        enddo
                  enddo
                  
                  amass(kblock,:,:) = 0.0D0
                  do igauss = 1, ngauss
                        call CALC_SHAPE_FUNCTIONS(
     &                         xi(igauss),eta(igauss),zeta(igauss), shapes)
                        call CALC_SHAPE_FUNCTIONS_DERIV(xi(igauss), eta(igauss),zeta(igauss), dNdzeta)
                        call JACOBIAN_FULL(COORD, dNdzeta, FJAC, DETJ)

                        mij = w(igauss) * DETJ * rho
                        do i = 1, nnode
                              do j = 1, nnode
                                amass(kblock,(i-1)*3+1, (j-1)*3+1) = amass(kblock,(i-1)*3+1, (j-1)*3+1) + mij*shapes(i)*shapes(j)
                                amass(kblock,(i-1)*3+2, (j-1)*3+2) = amass(kblock,(i-1)*3+2, (j-1)*3+2) + mij*shapes(i)*shapes(j)
                                amass(kblock,(i-1)*3+3, (j-1)*3+3) = amass(kblock,(i-1)*3+3, (j-1)*3+3) + mij*shapes(i)*shapes(j)
                              end do
                        end do
                  enddo

                  ! Row-sum lumping
                  rowMass = 0.0D0
                  do i=1,ndofel
                        do j=1,ndofel
                              rowMass(i)=rowMass(i)+amass(kblock,i,j)
                        enddo
                  enddo
                  do i=1,ndofel
                        do j=1,ndofel
                              if(i.eq.j) then
                                amass(kblock,i,j)=rowMass(i)
                              else
                                amass(kblock,i,j)=0.0D0
                              endif
                        enddo
                  enddo
            end do
            if(firstrun) write(*,*) "Mass matrix calculation completed."
            
      elseif(lflags(iOpCode).eq.jIntForceAndDtStable) then
            ! Internal force and stable time step calculation          
            if(firstrun) write(*,*) "Starting internal force calculation..."
            
            dt = dtimeCur

            do kblock = 1,nblock
                  if(firstrun) write(*,*) "  [Step 0] Processing element block", kblock
                  
                  if(firstrun) write(*,*) "  [Step 0.1] Calculating DMAT..."
                  CALL CALC_DDSDDE(DMAT, E, FNU)
                  if(firstrun) write(*,*) "  [Step 0.1] DMAT calculated."
                  
                  ! Log material properties and D matrix
                  if(firstrun) then
                        write(*,'(A,E15.8)') "  [DEBUG] E = ", E
                        write(*,'(A,F15.8)') "  [DEBUG] nu = ", FNU
                        write(*,'(A,F15.8)') "  [DEBUG] rho = ", rho
                        write(*,'(A,E15.8)') "  [DEBUG] dt = ", dt
                        write(*,*) "  [DEBUG] D matrix (6x6):"
                        do I=1,6
                              write(*,'(A,6E15.6)') "    ", 
     &                        (DMAT(I,J), J=1,6)
                        enddo
                  endif

                  if(firstrun) write(*,*) "  [Step 0.2] Extracting coordinates..."
                  do I=1,3
                        do J=1,8
                              ! ========== C++ MODIFICATION START ==========
                              ! 存储初始坐标 (来自 C++ 中的 coord)
                              COORD_INITIAL(J,I) = coords(kblock,J,I)
                              ! ========== C++ MODIFICATION END ==========
                              
                              ! Use current coordinates for rate-based formulation (check.md 2.2.4.1)
                              COORD(J,I) = coords(kblock,J,I) + 
     &                              u(kblock,J*3-3+I)
                              Velocity(J,I) = v(kblock, (J-1)*3+I)
                        enddo
                  enddo
                  if(firstrun) write(*,*) "  [Step 0.2] Coordinates extracted."
                  
                  ! Log coordinates for comparison with Excel
                  if(firstrun) then
                        write(*,*) "  [DEBUG] Node coordinates:"
                        do J=1,8
                              write(*,'(A,I1,A,3F12.6)') "    Node ", J, ": ", 
     &                        COORD(J,1), COORD(J,2), COORD(J,3)
                        enddo
                  endif

                  if(firstrun) write(*,*) "  [Step 0.3] Calculating B-bar..."
                  CALL CALC_B_BAR(BiI(1,1),COORD(1,2),COORD(1,3))
                  CALL CALC_B_BAR(BiI(1,2),COORD(1,3),COORD(1,1))
                  CALL CALC_B_BAR(BiI(1,3),COORD(1,1),COORD(1,2))
                  if(firstrun) write(*,*) "  [Step 0.3] B-bar calculated."
                  
                  ! Log H matrix (BiI) BEFORE normalization for comparison
                  if(firstrun) then
                        write(*,*) "  [DEBUG] H matrix before normalization:"
                        do J=1,8
                              write(*,'(A,I1,A,3F15.6)') "    Node ", J, ": ", 
     &                        BiI(J,1), BiI(J,2), BiI(J,3)
                        enddo
                  endif

                  if(firstrun) write(*,*) "  [Step 0.3.5] Calculating volume..."
                  CALL CALC_VOL_BBAR(BiI(1,1),COORD(1,1),VOL)
                  if(firstrun) write(*,*) "  [Step 0.3.5] Volume calculated."
                  
                  ! Log volume for comparison
                  if(firstrun) then
                        write(*,'(A,F15.6)') "  [DEBUG] Element volume = ", VOL
                  endif
                  
                  if(firstrun) write(*,*) "  [Step 0.3.6] Normalizing BiI..."
                  BiI = BiI / VOL
                  if(firstrun) write(*,*) "  [Step 0.3.6] BiI normalized."
                  
                  ! Log H matrix (BiI) AFTER normalization for comparison
                  if(firstrun) then
                        write(*,*) "  [DEBUG] H matrix after normalization:"
                        do J=1,8
                              write(*,'(A,I1,A,3F15.6)') "    Node ", J, ": ", 
     &                        BiI(J,1), BiI(J,2), BiI(J,3)
                        enddo
                  endif

                  if(firstrun) write(*,*) "  [Step 0.4] Calculating Jacobian..."
                  ! 计算当前雅可比矩阵 (j0)
                  CALL JACOBIAN_CENTER(FJAC, COORD)
                  CALL JACOBIAN_INVERSE(FJAC, DETJ, FJACINV)
                  
                  ! ========== C++ MODIFICATION START ==========
                  ! 计算初始雅可比矩阵 (j0bar)
                  CALL JACOBIAN_CENTER(FJAC_INITIAL, COORD_INITIAL)
                  CALL JACOBIAN_INVERSE(FJAC_INITIAL, DETJ_INITIAL, 
     &                                  FJACINV_INITIAL)
                  
                  ! 检查初始行列式
                  IF (ABS(DETJ_INITIAL) .LT. 1.0D-20) THEN
                     write(*,*) "ERROR: Initial determinant (j0bar) is zero!"
                     call exit(1)
                  END IF
                  
                  ! 计算 C++ 版本的体积比 (rj = j0 / j0bar)
                  rj = DETJ / DETJ_INITIAL
                  ! ========== C++ MODIFICATION END ==========
                  
                  if(firstrun) write(*,*) "  [Step 0.4] Jacobian calculated."
                  
                  ! Log Jacobian and inverse for comparison
                  if(firstrun) then
                        write(*,*) "  [DEBUG] Jacobian J:"
                        do I=1,3
                              write(*,'(A,3F15.6)') "    ", 
     &                        FJAC(I,1), FJAC(I,2), FJAC(I,3)
                        enddo
                        write(*,*) "  [DEBUG] Jacobian inverse J^-1:"
                        do I=1,3
                              write(*,'(A,3F15.6)') "    ", 
     &                        FJACINV(I,1), FJACINV(I,2), FJACINV(I,3)
                        enddo
                        write(*,'(A,F15.6)') "  [DEBUG] det(J) = ", DETJ
                  endif

                  ! --- 1. Single point integration for internal force (F_0) ---
                  if(firstrun) write(*,*) "  [Step 1] Reading state variables..."
                  strain = svars(kblock, 1:6)
                  stress = svars(kblock, 7:12)
                  if(firstrun) write(*,*) "  [Step 1] State variables read."

                  ! Log velocity for comparison
                  if(firstrun) then
                        write(*,*) "  [DEBUG] Velocity vector (24x1):"
                        do J=1,8
                              write(*,'(A,I1,A,3F15.8)') "    Node ", J, ": ", 
     &                        Velocity(J,1), Velocity(J,2), Velocity(J,3)
                        enddo
                  endif
                  
                  if(firstrun) write(*,*) "  [Step 1.1] Calculating strain rate..."
                  dudx = matmul(transpose(Velocity), BiI)
                  call GET_DSTRAIN(dstrain, dudx, dt)
                  if(firstrun) write(*,*) "  [Step 1.1] Strain rate calculated."
                  
                  ! Log strain rate and increment for comparison
                  if(firstrun) then
                        write(*,*) "  [DEBUG] Strain rate depsilon/dt:"
                        do I=1,6
                              write(*,'(A,I1,A,E15.8)') "    Component ", I, 
     &                        ": ", dstrain(I)/dt
                        enddo
                        write(*,*) "  [DEBUG] Strain increment depsilon:"
                        do I=1,6
                              write(*,'(A,I1,A,E15.8)') "    Component ", I, 
     &                        ": ", dstrain(I)
                        enddo
                  endif
                  
                  ! NOTE: Stress rotation is applied here but not in Excel
                  ! For zero stress this has no effect, but for non-zero stress
                  ! this accounts for rigid body rotation
                  if(firstrun) write(*,*) "  [Step 1.2] Rotating stress..."
                  call STRESS_ROTATE(stress, dudx, dt)
                  if(firstrun) write(*,*) "  [Step 1.2] Stress rotated."

                  if(firstrun) write(*,*) "  [Step 1.3] Updating stress..."
                  strain = strain + dstrain
                  stress = stress + matmul(DMAT, dstrain)
                  svars(kblock, 1:6) = strain
                  svars(kblock, 7:12) = stress
                  if(firstrun) write(*,*) "  [Step 1.3] Stress updated."
                  
                  ! Log strain and stress for comparison
                  if(firstrun) then
                        write(*,*) "  [DEBUG] Current strain epsilon:"
                        do I=1,6
                              write(*,'(A,I1,A,E15.8)') "    Component ", I, 
     &                        ": ", strain(I)
                        enddo
                        write(*,*) "  [DEBUG] Stress increment dsigma:"
                        dsigma_temp = matmul(DMAT, dstrain)
                        do I=1,6
                              write(*,'(A,I1,A,E15.8)') "    Component ", I, 
     &                        ": ", dsigma_temp(I)
                        enddo
                        write(*,*) "  [DEBUG] Current stress sigma:"
                        do I=1,6
                              write(*,'(A,I1,A,E15.8)') "    Component ", I, 
     &                        ": ", stress(I)
                        enddo
                  endif
                  
                  ! Calculate and output Mises stress (every step)
                  call CALC_MISES_STRESS(stress, mises_stress)
                  write(*,'(A,I0,A,I0,A,E15.8)') "  [INFO] Step ", kstep, 
     &                    " Inc ", kinc, " Mises stress: ", mises_stress
                  
                  if(firstrun) write(*,*) "  [Step 1.4] Forming B matrix..."
                  call FORM_B_MATRIX(BiI, B)
                  if(firstrun) write(*,*) "  [Step 1.4] B matrix formed."
                  
                  if(firstrun) write(*,*) "  [Step 1.5] Computing RHS..."
                  rhs(kblock,:) = matmul(transpose(B), stress) * DETJ * WG
                  if(firstrun) write(*,*) "  [Step 1.5] RHS computed."
                  
                  ! Output initial RHS for debugging
                  write(*,'(A,I0,A,I0,A)') "  [RHS_OUTPUT] Step ", kstep, 
     &                    " Inc ", kinc, " - Initial RHS (before hourglass):"
                  do I=1,24
                      write(*,'(A,I2,A,E15.8)') "    DOF ", I, 
     &                        ": ", rhs(kblock,I)
                  enddo
                  
                  ! Log internal force for comparison
                  if(firstrun) then
                        write(*,*) "  [DEBUG] Volume = ", DETJ * WG
                        write(*,*) "  [DEBUG] Internal force fint (24x1):"
                        do I=1,24
                              write(*,'(A,I2,A,E15.8)') "    DOF ", I, 
     &                        ": ", rhs(kblock,I)
                        enddo
                  endif

                  ! --- 2. Puso EAS stabilization (check.md 2.2.4.1) ---
                  
                  ! Step 2.1: Calculate gamma vectors
                  if(firstrun) write(*,*) "  [Step 2.1] Calculating gammas..."
                  call HOURGLASS_SHAPE_VECTORS(BiI, COORD, H_VECTORS, gammas)
                  if(firstrun) write(*,*) "  [Step 2.1] Gammas calculated."
                  
                  ! Log gamma vectors
                  if(firstrun) then
                        write(*,*) "  [DEBUG] Gamma vectors (8x4):"
                        do J=1,8
                              write(*,'(A,I1,A,4F15.8)') "    Node ", J, ": ", 
     &                        gammas(J,1), gammas(J,2), gammas(J,3), gammas(J,4)
                        enddo
                  endif
                  
                  ! Step 2.2: Calculate Cmtxh (corrected material matrix)
                  ! KEY: Using FJAC (not FJACINV) and rj for proper hat{J0^-1} calculation
                  if(firstrun) write(*,*) "  [Step 2.2] Calculating Cmtxh..."
                  
                  ! ========== C++ MODIFICATION START ==========
                  ! old: call GET_CMTXH(DMAT, FJAC, DETJ, C_tilde)
                  ! new: 传递 rj (j0/j0bar) 而不是 DETJ (j0)
                  call GET_CMTXH(DMAT, FJAC, rj, C_tilde)
                  ! ========== C++ MODIFICATION END ==========
                  
                  if(firstrun) write(*,*) "  [Step 2.2] Cmtxh calculated."
                  
                  ! Log C_tilde for comparison (first element only)
                  if(firstrun) then
                      write(*,*) "  [DEBUG] C vs C_tilde comparison:"
                      write(*,'(A,E15.6)') "    C(1,1) = ", DMAT(1,1)
                      write(*,'(A,E15.6)') "    C_tilde(1,1) = ", C_tilde(1,1)
                      write(*,'(A,F10.4)') "    Ratio = ", 
     &                    C_tilde(1,1)/DMAT(1,1)
                      write(*,*) "  [DEBUG] Expected for regular cube:"
                      write(*,*) "    Ratio should be O(1) for晓岚version"
                      write(*,*) "    If Ratio=16: using 1/sqrt (Puso?)"
                      write(*,*) "    If Ratio=1/16: using sqrt (晓岚?)"
                      write(*,*) "    If Ratio≈1: correct!"
                  endif
                  
                  ! Step 2.3: Get displacement increment delta_u
                  u_old = svars(kblock, 25:48)
                  do I=1,24
                      delta_u(I) = u(kblock,I) - u_old(I)
                  enddo
                  
                  ! Step 2.4: Calculate K matrices (check.md Eq. 469-481)
                  if(firstrun) write(*,*) "  [Step 2.4] Calculating K matrices..."
                  call CALC_K_MATRICES(C_tilde, DETJ*WG, Kmat, 
     &                                 K_alpha_u, K_alpha_alpha)
                  if(firstrun) write(*,*) "  [Step 2.4] K matrices calculated."
                  
                  ! Step 2.5: Invert K_alpha_alpha
                  if(firstrun) write(*,*) "  [Step 2.5] Inverting K_alpha_alpha..."
                  call INVERT_6X6(K_alpha_alpha, K_alpha_alpha_inv)
                  if(firstrun) write(*,*) "  [Step 2.5] K_alpha_alpha inverted."
                  
                  ! Step 2.6: Calculate hourglass force increment (check.md Eq. 444)
                  if(firstrun) write(*,*) "  [Step 2.6] Calculating df_hg..."
                  call CALC_DF_HG_EAS(Kmat, K_alpha_u, K_alpha_alpha_inv,
     &                                gammas, delta_u, transpose(FJAC), 
     &                                df_hg)
                  if(firstrun) write(*,*) "  [Step 2.6] df_hg calculated."

                  ! Step 2.7: Update hourglass forces (check.md Eq. 438)
                  if(firstrun) write(*,*) "  [Step 2.7] Updating hourglass forces..."
                  do I=1,4
                      f_hg_old(:,I) = svars(kblock, 12+(I-1)*3+1:12+(I-1)*3+3)
                  enddo
                  f_hg_new = f_hg_old + df_hg
                  do I=1,4
                      svars(kblock, 12+(I-1)*3+1:12+(I-1)*3+3) = f_hg_new(:,I)
                  enddo
                  if(firstrun) write(*,*) "  [Step 2.7] Hourglass forces updated."
                  
                  ! Output hourglass force details for debugging
                  write(*,'(A,I0,A,I0,A)') "  [FHG_DETAILS] Step ", kstep, 
     &                    " Inc ", kinc, " - Hourglass force details:"
                  do I=1,4
                      write(*,'(A,I1,A,3E15.8)') "    f_hg_new(", I, "): ", 
     &                        f_hg_new(1,I), f_hg_new(2,I), f_hg_new(3,I)
                  enddo
                  
                  ! Step 2.8: Calculate stabilization force (check.md Eq. 436)
                  ! F_stab = Σ Γ_i * J0 * f_i * (V/8)
                  if(firstrun) write(*,*) "  [Step 2.8] Calculating F_stab..."
                  fHG = 0.0D0
                  do I = 1, 4
                      do J = 1, 8
                          fHG(J*3-2)=fHG(J*3-2)+gammas(J,I)*(
     *                    FJAC(1,1)*f_hg_new(1,I)+FJAC(1,2)*f_hg_new(2,I)
     *                    +FJAC(1,3)*f_hg_new(3,I))
                          fHG(J*3-1)=fHG(J*3-1)+gammas(J,I)*(
     *                    FJAC(2,1)*f_hg_new(1,I)+FJAC(2,2)*f_hg_new(2,I)
     *                    +FJAC(2,3)*f_hg_new(3,I))
                          fHG(J*3-0)=fHG(J*3-0)+gammas(J,I)*(
     *                    FJAC(3,1)*f_hg_new(1,I)+FJAC(3,2)*f_hg_new(2,I)
     *                    +FJAC(3,3)*f_hg_new(3,I))
                      enddo
                  enddo
                  ! Apply V/8 factor (check.md note after Eq. 575)
                  ! Apply tuning scale for hourglass force
                  fHG = fHG * (DETJ*WG / 8.0D0) * SCALE_HOURGLASS
                  if(firstrun) write(*,*) "  [Step 2.8] F_stab calculated."
                  
                  ! Output FHG for debugging
                  write(*,'(A,I0,A,I0,A)') "  [FHG_OUTPUT] Step ", kstep, 
     &                    " Inc ", kinc, " - Hourglass force FHG:"
                  do I=1,24
                      write(*,'(A,I2,A,E15.8)') "    DOF ", I, 
     &                        ": ", fHG(I)
                  enddo
                  
                  ! Step 2.9: Add stabilization force to RHS
                  if(firstrun) write(*,*) "  [Step 2.9] Adding F_stab to RHS..."
                  rhs(kblock,:) = rhs(kblock,:) + fHG
                  if(firstrun) write(*,*) "  [Step 2.9] RHS updated."
                  
                  ! Output final RHS for debugging
                  write(*,'(A,I0,A,I0,A)') "  [RHS_FINAL] Step ", kstep, 
     &                    " Inc ", kinc, " - Final RHS (after hourglass):"
                  do I=1,24
                      write(*,'(A,I2,A,E15.8)') "    DOF ", I, 
     &                        ": ", rhs(kblock,I)
                  enddo
                  
                  ! Summary of RHS changes
                  write(*,'(A,I0,A,I0,A)') "  [RHS_SUMMARY] Step ", kstep, 
     &                    " Inc ", kinc, " - RHS change summary:"
                  write(*,'(A,E15.8)') "    Volume factor (DETJ*WG/8): ", 
     &                        DETJ*WG/8.0D0
                  write(*,'(A,E15.8)') "    Hourglass scale factor: ", 
     &                        SCALE_HOURGLASS
                  write(*,'(A,E15.8)') "    Total scaling factor: ", 
     &                        (DETJ*WG/8.0D0)*SCALE_HOURGLASS
                  
                  ! Step 2.10: Update stored displacement
                  svars(kblock, 25:48) = u(kblock,:)

            end do
            if(firstrun) write(*,*) "Internal force calculation completed."
      endif

      firstrun = .false.
      RETURN
      END

      SUBROUTINE JACOBIAN_CENTER(JAC, COORD)
      use constants_module
      IMPLICIT NONE
      real*8, intent(out) :: JAC(3,3)
      real*8, intent(in) :: COORD(8,3)
      JAC = matmul(transpose(XiI), COORD) * one_over_eight
      JAC = transpose(JAC)
      RETURN
      END

      SUBROUTINE JACOBIAN_INVERSE(JAC, DETJ, JACINV)
      IMPLICIT NONE
      REAL*8 JAC(3,3), JACINV(3,3), DETJ
      DETJ = JAC(1,1)*(JAC(2,2)*JAC(3,3)-JAC(2,3)*JAC(3,2)) -
     1       JAC(1,2)*(JAC(2,1)*JAC(3,3)-JAC(2,3)*JAC(3,1)) +
     2       JAC(1,3)*(JAC(2,1)*JAC(3,2)-JAC(2,2)*JAC(3,1))
      IF (ABS(DETJ) .LT. 1.0D-20) THEN
        write(*,*) "ERROR: Jacobian determinant is zero or too small!"
        STOP
      END IF
      JACINV(1,1) = (JAC(2,2)*JAC(3,3)-JAC(2,3)*JAC(3,2))/DETJ
      JACINV(1,2) = (JAC(1,3)*JAC(3,2)-JAC(1,2)*JAC(3,3))/DETJ
      JACINV(1,3) = (JAC(1,2)*JAC(2,3)-JAC(1,3)*JAC(2,2))/DETJ
      JACINV(2,1) = (JAC(2,3)*JAC(3,1)-JAC(2,1)*JAC(3,3))/DETJ
      JACINV(2,2) = (JAC(1,1)*JAC(3,3)-JAC(1,3)*JAC(3,1))/DETJ
      JACINV(2,3) = (JAC(1,3)*JAC(2,1)-JAC(1,1)*JAC(2,3))/DETJ
      JACINV(3,1) = (JAC(2,1)*JAC(3,2)-JAC(2,2)*JAC(3,1))/DETJ
      JACINV(3,2) = (JAC(1,2)*JAC(3,1)-JAC(1,1)*JAC(3,2))/DETJ
      JACINV(3,3) = (JAC(1,1)*JAC(2,2)-JAC(1,2)*JAC(2,1))/DETJ
      RETURN
      END

      SUBROUTINE GET_DSTRAIN(dstrain, dudx, dt)
      implicit none
      real*8, intent(in) :: dudx(3,3), dt
      real*8, intent(out) :: dstrain(6)
      real*8 D(3,3)
      D = 0.5D0 * (dudx + transpose(dudx))
      dstrain(1) = D(1,1) * dt; dstrain(2) = D(2,2) * dt
      dstrain(3) = D(3,3) * dt; dstrain(4) = 2.0D0 * D(1,2) * dt
      dstrain(5) = 2.0D0 * D(2,3) * dt; dstrain(6) = 2.0D0 * D(1,3) * dt
      RETURN
      END

      SUBROUTINE STRESS_ROTATE(stress, dudx, dt)
      implicit none
      real*8, intent(inout) :: stress(6)
      real*8, intent(in) :: dudx(3,3), dt
      real*8 W(3,3), S(3,3), S_new(3,3), dS(3,3)
      S(1,1)=stress(1); S(2,2)=stress(2); S(3,3)=stress(3)
      S(1,2)=stress(4); S(2,1)=stress(4); S(2,3)=stress(5)
      S(3,2)=stress(5); S(1,3)=stress(6); S(3,1)=stress(6)
      W = 0.5D0 * (dudx - transpose(dudx))
      dS = matmul(W, S) - matmul(S, W)
      S_new = S + dS * dt
      stress(1)=S_new(1,1); stress(2)=S_new(2,2); stress(3)=S_new(3,3)
      stress(4)=S_new(1,2); stress(5)=S_new(2,3); stress(6)=S_new(1,3)
      RETURN
      END

      SUBROUTINE FORM_B_MATRIX(BiI, B)
      IMPLICIT NONE
      REAL*8, INTENT(IN)  :: BiI(8,3)
      REAL*8, INTENT(OUT) :: B(6,24)
      INTEGER :: K
      B = 0.D0
      DO K = 1, 8
        B(1,3*K-2)=BiI(K,1); B(2,3*K-1)=BiI(K,2); B(3,3*K)=BiI(K,3)
        B(4,3*K-2)=BiI(K,2); B(4,3*K-1)=BiI(K,1)
        B(5,3*K-1)=BiI(K,3); B(5,3*K)=BiI(K,2)
        B(6,3*K-2)=BiI(K,3); B(6,3*K)=BiI(K,1)
      END DO
      RETURN
      END
      
      SUBROUTINE HOURGLASS_SHAPE_VECTORS(BiI, COORD, h, gammas)
      use constants_module
      implicit none
      real*8, intent(in) :: BiI(8,3), COORD(8,3), h(8,4)
      real*8, intent(out) :: gammas(8,4)
      real*8 :: h_dot_x(3)
      integer :: i, A
      ! 按照check.md公式 γⱼ = (1/8)[hⱼ - Σᵢ(hⱼ·xᵢ)bᵢ]
      do i = 1, 4
        ! 计算 hⱼ·xᵢ 对于所有3个方向
        h_dot_x = 0.0D0
        do A = 1, 8
          h_dot_x(1) = h_dot_x(1) + h(A,i) * COORD(A,1)
          h_dot_x(2) = h_dot_x(2) + h(A,i) * COORD(A,2)
          h_dot_x(3) = h_dot_x(3) + h(A,i) * COORD(A,3)
        enddo
        ! 计算 γ = (1/8) * [h - (h·x) * b]
        do A = 1, 8
          gammas(A,i) = SCALE_GAMMA * one_over_eight * (h(A,i) - 
     &                  (h_dot_x(1)*BiI(A,1) + h_dot_x(2)*BiI(A,2) + 
     &                   h_dot_x(3)*BiI(A,3)))
        enddo
      enddo
      RETURN
      END

      SUBROUTINE CALC_K_MATRICES(C_tilde, VOL, Kmat, K_alpha_u, 
     &                           K_alpha_alpha)
      ! Calculate K matrices according to check.md Eq. 472-493
      ! CRITICAL FIX: Use single transpose B^T*C*B (not double B^T*C*B^T)
      ! This means K^ij matrices are FULL 3x3 matrices with off-diagonal terms!
      ! Kmat now stores ALL K^ij combinations: Kmat(i,j,:,:) = K^ij
      use constants_module
      implicit none
      real*8, intent(in) :: C_tilde(6,6), VOL
      real*8, intent(out) :: Kmat(4,4,3,3), K_alpha_u(4,6,3)
      real*8, intent(out) :: K_alpha_alpha(6,6)
      real*8 :: factor_K123, factor_K4, factor_Kau, H, H43, H51, H62
      real*8 :: C(6,6)  ! Local copy for convenience
      integer :: i, j
      
      ! Initialize
      Kmat = 0.0D0
      K_alpha_u = 0.0D0
      K_alpha_alpha = 0.0D0
      C = C_tilde
      
      ! Define factors according to check.md (with tuning scale)
      factor_K123 = (8.0D0/3.0D0) * SCALE_K_MATRIX  ! For K^11, K^22, K^33 and cross terms
      factor_K4 = (8.0D0/9.0D0) * SCALE_K_MATRIX    ! For K^44
      factor_Kau = (8.0D0/3.0D0) * SCALE_K_MATRIX   ! For K_alpha_u
      
      ! ========== DIAGONAL TERMS ==========
      
      ! ===== K^11 = (8/3)*(B11^T*C*B11 + B12^T*C*B12) =====
      ! B11 = [0  e2  e5], B12 = [e1  0  e6]
      Kmat(1,1,1,1) = factor_K123 * C(1,1)
      Kmat(1,1,1,3) = factor_K123 * C(1,6)
      Kmat(1,1,2,2) = factor_K123 * C(2,2)
      Kmat(1,1,2,3) = factor_K123 * C(2,5)
      Kmat(1,1,3,1) = factor_K123 * C(6,1)
      Kmat(1,1,3,2) = factor_K123 * C(5,2)
      Kmat(1,1,3,3) = factor_K123 * (C(5,5) + C(6,6))
      
      ! ===== K^22 = (8/3)*(B21^T*C*B21 + B23^T*C*B23) =====
      ! B21 = [0  e5  e3], B23 = [e1  e4  0]
      Kmat(2,2,1,1) = factor_K123 * C(1,1)
      Kmat(2,2,1,2) = factor_K123 * C(1,4)
      Kmat(2,2,2,1) = factor_K123 * C(4,1)
      Kmat(2,2,2,2) = factor_K123 * (C(5,5) + C(4,4))
      Kmat(2,2,2,3) = factor_K123 * C(5,3)
      Kmat(2,2,3,2) = factor_K123 * C(3,5)
      Kmat(2,2,3,3) = factor_K123 * C(3,3)
      
      ! ===== K^33 = (8/3)*(B32^T*C*B32 + B33^T*C*B33) =====
      ! B32 = [e6  0  e3], B33 = [e4  e2  0]
      Kmat(3,3,1,1) = factor_K123 * (C(6,6) + C(4,4))
      Kmat(3,3,1,2) = factor_K123 * C(4,2)
      Kmat(3,3,1,3) = factor_K123 * C(6,3)
      Kmat(3,3,2,1) = factor_K123 * C(2,4)
      Kmat(3,3,2,2) = factor_K123 * C(2,2)
      Kmat(3,3,3,1) = factor_K123 * C(3,6)
      Kmat(3,3,3,3) = factor_K123 * C(3,3)
      
      ! ===== K^44 = (8/9)*(B44^T*C*B44 + B55^T*C*B55 + B66^T*C*B66) =====
      ! B44 = [0  0  e3], B55 = [e1  0  0], B66 = [0  e2  0]
      ! K^44 is diagonal (special case)
      Kmat(4,4,1,1) = factor_K4 * C(1,1)
      Kmat(4,4,2,2) = factor_K4 * C(2,2)
      Kmat(4,4,3,3) = factor_K4 * C(3,3)
      
      ! ========== CROSS TERMS (check.md Eq. 476-477, 480-481, 484-485) ==========
      
      ! ===== K^12 = (8/3) * B11^T * C * B21 =====
      ! B11 = [0  e2  e5], B21 = [0  e5  e3]
      Kmat(1,2,2,2) = factor_K123 * C(2,5)
      Kmat(1,2,2,3) = factor_K123 * C(2,3)
      Kmat(1,2,3,2) = factor_K123 * C(5,5)
      Kmat(1,2,3,3) = factor_K123 * C(5,3)
      
      ! ===== K^13 = (8/3) * B12^T * C * B32 =====
      ! B12 = [e1  0  e6], B32 = [e6  0  e3]
      Kmat(1,3,1,1) = factor_K123 * C(1,6)
      Kmat(1,3,1,3) = factor_K123 * C(1,3)
      Kmat(1,3,3,1) = factor_K123 * C(6,6)
      Kmat(1,3,3,3) = factor_K123 * C(6,3)
      
      ! ===== K^21 = (8/3) * B21^T * C * B11 =====
      ! B21 = [0  e5  e3], B11 = [0  e2  e5]
      Kmat(2,1,2,2) = factor_K123 * C(5,2)
      Kmat(2,1,2,3) = factor_K123 * C(5,5)
      Kmat(2,1,3,2) = factor_K123 * C(3,2)
      Kmat(2,1,3,3) = factor_K123 * C(3,5)
      
      ! ===== K^23 = (8/3) * B23^T * C * B33 =====
      ! B23 = [e1  e4  0], B33 = [e4  e2  0]
      Kmat(2,3,1,1) = factor_K123 * C(1,4)
      Kmat(2,3,1,2) = factor_K123 * C(1,2)
      Kmat(2,3,2,1) = factor_K123 * C(4,4)
      Kmat(2,3,2,2) = factor_K123 * C(4,2)
      
      ! ===== K^31 = (8/3) * B32^T * C * B12 =====
      ! B32 = [e6  0  e3], B12 = [e1  0  e6]
      Kmat(3,1,1,1) = factor_K123 * C(6,1)
      Kmat(3,1,1,3) = factor_K123 * C(6,6)
      Kmat(3,1,3,1) = factor_K123 * C(3,1)
      Kmat(3,1,3,3) = factor_K123 * C(3,6)
      
      ! ===== K^32 = (8/3) * B33^T * C * B23 =====
      ! B33 = [e4  e2  0], B23 = [e1  e4  0]
      Kmat(3,2,1,1) = factor_K123 * C(4,1)
      Kmat(3,2,1,2) = factor_K123 * C(4,4)
      Kmat(3,2,2,1) = factor_K123 * C(2,1)
      Kmat(3,2,2,2) = factor_K123 * C(2,4)
      
      ! NOTE: K^14, K^24, K^34, K^41, K^42, K^43 are not defined in check.md
      ! They remain zero (already initialized)
      
      ! Calculate K_alpha_u according to check.md Eq. 488-491
      ! K_alpha_u^1 = (8/3)*[...]
      K_alpha_u(1,1,2) = factor_Kau*C(1,2)
      K_alpha_u(1,1,3) = factor_Kau*C(1,5)
      K_alpha_u(1,2,1) = factor_Kau*C(2,1)
      K_alpha_u(1,2,3) = factor_Kau*C(2,6)
      
      ! K_alpha_u^2
      K_alpha_u(2,1,2) = factor_Kau*C(1,5)
      K_alpha_u(2,1,3) = factor_Kau*C(1,3)
      K_alpha_u(2,3,1) = factor_Kau*C(3,1)
      K_alpha_u(2,3,2) = factor_Kau*C(3,4)
      
      ! K_alpha_u^3
      K_alpha_u(3,2,1) = factor_Kau*C(2,6)
      K_alpha_u(3,2,3) = factor_Kau*C(2,3)
      K_alpha_u(3,3,1) = factor_Kau*C(3,4)
      K_alpha_u(3,3,2) = factor_Kau*C(3,2)
      
      ! K_alpha_u^4 (uses 8/9 factor, matching K^44)
      H43 = C(1,3) + C(2,3) + C(3,3)
      H51 = C(1,1) + C(2,1) + C(3,1)
      H62 = C(1,2) + C(2,2) + C(3,2)
      K_alpha_u(4,4,3) = factor_K4*H43  ! Note: 8/9, not 8/3
      K_alpha_u(4,5,1) = factor_K4*H51  ! Note: 8/9, not 8/3
      K_alpha_u(4,6,2) = factor_K4*H62  ! Note: 8/9, not 8/3
      
      ! Calculate K_alpha_alpha according to check.md Eq. 493
      H = C(1,1) + C(2,2) + C(3,3) + 
     &    2.0D0*(C(1,2) + C(2,3) + C(1,3))
      
      ! K_alpha_alpha uses 8/3 factor (check.md Eq. 493)
      K_alpha_alpha(1,1) = factor_Kau*C(1,1)
      K_alpha_alpha(2,2) = factor_Kau*C(2,2)
      K_alpha_alpha(3,3) = factor_Kau*C(3,3)
      K_alpha_alpha(4,4) = factor_Kau*H/3.0D0
      K_alpha_alpha(5,5) = factor_Kau*H/3.0D0
      K_alpha_alpha(6,6) = factor_Kau*H/3.0D0
      
      RETURN
      END
      
      SUBROUTINE INVERT_6X6(A, Ainv)
      ! Invert 6x6 diagonal matrix
      implicit none
      real*8, intent(in) :: A(6,6)
      real*8, intent(out) :: Ainv(6,6)
      integer :: i
      
      Ainv = 0.0D0
      do i = 1, 6
          if (abs(A(i,i)) > 1.0D-20) then
              Ainv(i,i) = 1.0D0 / A(i,i)
          else
              Ainv(i,i) = 0.0D0
          endif
      enddo
      
      RETURN
      END
      
      SUBROUTINE CALC_DF_HG_EAS(Kmat, K_alpha_u, K_alpha_alpha_inv,
     &                          gammas, delta_u, J0_T, df_hg)
      ! Calculate hourglass force increment using EAS condensation
      ! According to check.md Eq. 444 (CORRECTED):
      ! df_i = K_i * delta_u
      ! K_i = Σ_j (K^ij - K_αu^iT * K_αα^-1 * K_αu^j) * J₀ᵀ * Γ_j^T
      implicit none
      real*8, intent(in) :: Kmat(4,4,3,3), K_alpha_u(4,6,3)
      real*8, intent(in) :: K_alpha_alpha_inv(6,6), gammas(8,4)
      real*8, intent(in) :: delta_u(24), J0_T(3,3)
      real*8, intent(out) :: df_hg(3,4)
      real*8 :: K_i(3,24), Gamma_j_T(3,24), K_ij_condensed(3,3)
      real*8 :: K_au_i_T(3,6), K_aa_inv_K_au_j(6,3), temp33(3,3)
      real*8 :: J0T_Gamma_j_T(3,24)
      integer :: i, j, node, alpha, beta
      
      df_hg = 0.0D0
      
      do i = 1, 4
          ! Build K_i for mode i: K_i = Σ_j (...) * Γ_j^T
          K_i = 0.0D0
          
          do j = 1, 4
              ! Step 1: Form Gamma_j^T (3x24 matrix)
              Gamma_j_T = 0.0D0
              do node = 1, 8
                  Gamma_j_T(1, node*3-2) = gammas(node, j)
                  Gamma_j_T(2, node*3-1) = gammas(node, j)
                  Gamma_j_T(3, node*3-0) = gammas(node, j)
              enddo
              
              ! Step 2: Calculate K^ij - K_αu^iT * K_αα^-1 * K_αu^j (3x3)
              ! K_αu^iT is 3x6, K_αα^-1 is 6x6, K_αu^j is 6x3
              ! Result is 3x3
              
              ! K_αu^iT (transpose of K_alpha_u(i,:,:) which is 6x3)
              K_au_i_T = transpose(K_alpha_u(i,:,:))  ! 3x6
              
              ! K_αα^-1 * K_αu^j
              K_aa_inv_K_au_j = matmul(K_alpha_alpha_inv,  
     &                                  K_alpha_u(j,:,:))  ! 6x3
              
              ! K_αu^iT * (K_αα^-1 * K_αu^j)
              temp33 = matmul(K_au_i_T, K_aa_inv_K_au_j)  ! 3x3
              
              ! K^ij - K_αu^iT * K_αα^-1 * K_αu^j
              ! CRITICAL FIX: Use Kmat(i,j,:,:) not Kmat(i,:,:)!
              ! Each (i,j) pair has its own K^ij matrix (including cross terms)
              K_ij_condensed = Kmat(i,j,:,:) - temp33  ! 3x3
              
              ! Step 3: Apply J₀ᵀ transformation (check.md Eq. 444)
              ! J0T_Gamma_j_T = J₀ᵀ * Gamma_j_T (3x3) * (3x24) = (3x24)
              J0T_Gamma_j_T = matmul(J0_T, Gamma_j_T)
              
              ! Step 4: (K^ij_condensed) * J₀ᵀ * Γ_j^T gives 3x24
              K_i = K_i + matmul(K_ij_condensed, J0T_Gamma_j_T)  ! 3x24
          enddo
          
          ! Step 4: df_hg(:,i) = K_i * delta_u
          df_hg(:,i) = matmul(K_i, delta_u)  ! 3x1
      enddo
      
      RETURN
      END

      


      SUBROUTINE GET_CMTXH(DMAT, FJAC, rj_in, Cmtxh)
      !
      ! C++ MODIFICATION: 
      ! 接收 C++ 风格的 rj_in (j0/j0bar) 作为输入
      ! 移除了内部的 j0, j_bar_0, rj 计算
      !
      use constants_module
      IMPLICIT NONE
      REAL*8, INTENT(IN) :: DMAT(6,6), FJAC(3,3), rj_in
      REAL*8, INTENT(OUT) :: Cmtxh(6,6)
      REAL*8 :: J0_T(3,3), R(3,3), U_diag_inv(3,3)
      REAL*8 :: hat_J0_inv(3,3)
      
      !
      ! Step 1: Calculate J0^T at element center
      J0_T = transpose(FJAC)
      
      !
      ! Step 2: Polar decomposition to get R and U_diag_inv
      ! (这部分不变，它计算 J0hInv)
      call POLAR_DECOMP_FOR_J0HINV(J0_T, R, U_diag_inv)
      
      !
      ! Step 3: Calculate hat{J0^-1} = R * U_diag_inv
      ! (这对应于 C++ 中的 J0hInv)
      hat_J0_inv = matmul(R, U_diag_inv)
      
      !
      ! Step 4: C++ MODIFICATION - 移除旧的 rj 计算
      !
      
      !
      ! Step 5: C++ MODIFICATION - 应用 J^T * D * J 变换 (无 rj)
      !
      ! old: call ROT_DMTX(DMAT, hat_J0_inv, rj, Cmtxh)
      call ROT_DMTX(DMAT, hat_J0_inv, Cmtxh)
      
      !
      ! Step 6: C++ MODIFICATION - 在变换后应用 rj 和缩放因子
      ! (这对应 C++ 中的 Ch.Plus(D, rj))
      Cmtxh = Cmtxh * rj_in * SCALE_C_TILDE
      
      RETURN
      END

      SUBROUTINE POLAR_DECOMP_FOR_J0HINV(J0_T, R, U_diag_inv)
      ! Polar decomposition: J0_T = R * U
      ! This implements the C++ enhanced hourglass algorithm logic
      ! Key difference: U_diag_inv = diag(1/||j1||, 1/||j2||, 1/||j3||)
      ! This is the CRITICAL difference from the original implementation!
      IMPLICIT NONE
      REAL*8, INTENT(IN) :: J0_T(3,3)
      REAL*8, INTENT(OUT) :: R(3,3), U_diag_inv(3,3)
      REAL*8 :: j1(3), j2(3), j3(3)
      REAL*8 :: j1_norm, j2_norm, j3_norm
      REAL*8 :: q1(3), q2(3), q3(3)  ! Orthonormal basis
      INTEGER :: i
      
      ! Extract column vectors from J0_T
      ! J0 = [j1 j2 j3], so J0_T = [j1^T; j2^T; j3^T]
      do i = 1, 3
          j1(i) = J0_T(1,i)  ! First row of J0_T = First column of J0
          j2(i) = J0_T(2,i)
          j3(i) = J0_T(3,i)
      enddo
      
      ! Calculate norms: ||j_i|| = sqrt(j_i · j_i)
      j1_norm = sqrt(dot_product(j1, j1))
      j2_norm = sqrt(dot_product(j2, j2))
      j3_norm = sqrt(dot_product(j3, j3))
      
      ! Gram-Schmidt orthogonalization to get R
      ! q1 = j1 / ||j1||
      q1 = j1 / j1_norm
      
      ! q2 = j2 - (j2·q1)q1, then normalize
      q2 = j2 - dot_product(j2, q1) * q1
      q2 = q2 / sqrt(dot_product(q2, q2))
      
      ! q3 = j3 - (j3·q1)q1 - (j3·q2)q2, then normalize
      q3 = j3 - dot_product(j3, q1) * q1 - dot_product(j3, q2) * q2
      q3 = q3 / sqrt(dot_product(q3, q3))
      
      ! R matrix (orthogonal)
      R(1,:) = q1
      R(2,:) = q2
      R(3,:) = q3
      
      ! CRITICAL: U_diag_inv = diag(1/||j1||, 1/||j2||, 1/||j3||)
      ! This is the key difference from the original implementation!
      U_diag_inv = 0.0D0
      U_diag_inv(1,1) = 1.0D0 / j1_norm  ! 1/sqrt(j1·j1)
      U_diag_inv(2,2) = 1.0D0 / j2_norm  ! 1/sqrt(j2·j2)
      U_diag_inv(3,3) = 1.0D0 / j3_norm  ! 1/sqrt(j3·j3)
      
      RETURN
      END

      SUBROUTINE ROT_DMTX(D, J0Inv, D_rotated)
      !
      ! C++ MODIFICATION: 移除了 rj 输入参数.
      ! 此子程序现在只计算 D_rotated = J^T * D * J
      !
      IMPLICIT NONE
      REAL*8, INTENT(IN) :: D(6,6), J0Inv(3,3)
      REAL*8, INTENT(OUT) :: D_rotated(6,6)
      REAL*8 :: J_transform(6,6), temp(6,6)
      REAL*8 :: j11, j12, j13, j21, j22, j23, j31, j32, j33
      
      !
      ! Extract components of J0Inv (hat{J0^-1})
      j11 = J0Inv(1,1); j12 = J0Inv(1,2); j13 = J0Inv(1,3)
      j21 = J0Inv(2,1); j22 = J0Inv(2,2); j23 = J0Inv(2,3)
      j31 = J0Inv(3,1); j32 = J0Inv(3,2); j33 = J0Inv(3,3)
      
      !
      ! Build the 6x6 J_transform matrix
      ! (这部分不变)
      J_transform = 0.0D0
      
      ! Row 1
      J_transform(1,1) = j11*j11
      J_transform(1,2) = j21*j21
      J_transform(1,3) = j31*j31
      J_transform(1,4) = j11*j21
      J_transform(1,5) = j21*j31
      J_transform(1,6) = j11*j31
      
      ! Row 2
      J_transform(2,1) = j12*j12
      J_transform(2,2) = j22*j22
      J_transform(2,3) = j32*j32
      J_transform(2,4) = j12*j22
      J_transform(2,5) = j22*j32
      J_transform(2,6) = j12*j32
      
      ! Row 3
      J_transform(3,1) = j13*j13
      J_transform(3,2) = j23*j23
      J_transform(3,3) = j33*j33
      J_transform(3,4) = j13*j23
      J_transform(3,5) = j23*j33
      J_transform(3,6) = j13*j33
      
      ! Row 4
      J_transform(4,1) = 2.0D0*j11*j12
      J_transform(4,2) = 2.0D0*j21*j22
      J_transform(4,3) = 2.0D0*j31*j32
      J_transform(4,4) = j11*j22 + j21*j12
      J_transform(4,5) = j21*j32 + j31*j22
      J_transform(4,6) = j11*j32 + j31*j12
      
      ! Row 5
      J_transform(5,1) = 2.0D0*j12*j13
      J_transform(5,2) = 2.0D0*j22*j23
      J_transform(5,3) = 2.0D0*j32*j33
      J_transform(5,4) = j12*j23 + j22*j13
      J_transform(5,5) = j22*j33 + j32*j23
      J_transform(5,6) = j12*j33 + j32*j13
      
      ! Row 6
      J_transform(6,1) = 2.0D0*j13*j11
      J_transform(6,2) = 2.0D0*j23*j21
      J_transform(6,3) = 2.0D0*j33*j31
      J_transform(6,4) = j13*j21 + j23*j11
      J_transform(6,5) = j23*j31 + j33*j21
      J_transform(6,6) = j13*j31 + j33*j11
      
      !
      ! C++ MODIFICATION: 
      ! old: D_rotated = rj * matmul(transpose(J_transform), temp) 
      ! new: 移除 rj 缩放
      temp = matmul(D, J_transform)
      D_rotated = matmul(transpose(J_transform), temp)
      
      RETURN
      END

      SUBROUTINE CALC_DDSDDE(DDSDDE, E, NU)
      IMPLICIT NONE
      REAL*8, INTENT(OUT) :: DDSDDE(6,6)
      REAL*8, INTENT(IN) :: E, NU
      REAL*8 :: C1, C2
      C1 = E / (1.D0 + NU) / (1.D0 - 2.D0*NU)
      C2 = E / (2.D0 * (1.D0 + NU))
      DDSDDE=0.D0
      DDSDDE(1,1)=C1*(1.D0-NU); DDSDDE(1,2)=C1*NU; DDSDDE(1,3)=C1*NU
      DDSDDE(2,1)=C1*NU; DDSDDE(2,2)=C1*(1.D0-NU); DDSDDE(2,3)=C1*NU
      DDSDDE(3,1)=C1*NU; DDSDDE(3,2)=C1*NU; DDSDDE(3,3)=C1*(1.D0-NU)
      DDSDDE(4,4)=C2; DDSDDE(5,5)=C2; DDSDDE(6,6)=C2
      RETURN
      END

      SUBROUTINE CALC_B_BAR(BiI, y, z)
      IMPLICIT NONE
      REAL*8,INTENT(OUT) :: BiI(8)
      REAL*8,INTENT(IN) :: y(8), z(8)
      BiI(1)=-(y(2)*(z(3)+z(4)-z(5)-z(6))+y(3)*(-z(2)+z(4))
     1+y(4)*(-z(2)-z(3)+z(5)+z(8))+y(5)*(z(2)-z(4)+z(6)-z(8))
     2+y(6)*(z(2)-z(5))+y(8)*(-z(4)+z(5)))/12.D0
      BiI(2)=(y(1)*(z(3)+z(4)-z(5)-z(6))+y(3)*(-z(1)-z(4)+z(6)+z(7))
     1+y(4)*(-z(1)+z(3))+y(5)*(z(1)-z(6)) 
     2+y(6)*(z(1)-z(3)+z(5)-z(7))+y(7)*(-z(3)+z(6)))/12.D0
      BiI(3)=-(y(1)*(z(2)-z(4))+y(2)*(-z(1)-z(4)+z(6)+z(7))
     1+y(4)*(z(1)+z(2)-z(7)-z(8))+y(6)*(-z(2)+z(7))
     2+y(7)*(-z(2)+z(4)-z(6)+z(8))+y(8)*(z(4)-z(7)))/12.D0
      BiI(4)=-(y(1)*(z(2)+z(3)-z(5)-z(8))+y(2)*(-z(1)+z(3))
     1+y(3)*(-z(1)-z(2)+z(7)+z(8))+y(5)*(z(1)-z(8))
     2+y(7)*(-z(3)+z(8))+y(8)*(z(1)-z(3)+z(5)-z(7)))/12.D0
      BiI(5)=(y(1)*(z(2)-z(4)+z(6)-z(8))+y(2)*(-z(1)+z(6))
     1+y(4)*(z(1)-z(8))+y(6)*(-z(1)-z(2)+z(7)+z(8))
     2+y(7)*(-z(6)+z(8))+y(8)*(z(1)+z(4)-z(6)-z(7)))/12.D0
      BiI(6)=(y(1)*(z(2)-z(5))+y(2)*(-z(1)+z(3)-z(5)+z(7))
     1+y(3)*(-z(2)+z(7))+y(5)*(z(1)+z(2)-z(7)-z(8))
     2+y(7)*(-z(2)-z(3)+z(5)+z(8))+y(8)*(z(5)-z(7)))/12.D0
      BiI(7)=(y(2)*(z(3)-z(6))+y(3)*(-z(2)+z(4)-z(6)+z(8))
     1+y(4)*(-z(3)+z(8))+y(5)*(z(6)-z(8))
     2+y(6)*(z(2)+z(3)-z(5)-z(8))+y(8)*(-z(3)-z(4)+z(5)+z(6)))/12.D0
      BiI(8)=-(y(1)*(z(4)-z(5))+y(3)*(-z(4)+z(7))
     1+y(4)*(-z(1)+z(3)-z(5)+z(7))+y(5)*(z(1)+z(4)-z(6)-z(7))
     2+y(6)*(z(5)-z(7))+y(7)*(-z(3)-z(4)+z(5)+z(6)))/12.D0
      RETURN
      END

C     --- Auxiliary routines for mass matrix ---
      SUBROUTINE CALC_SHAPE_FUNCTIONS(xi, eta, zeta, N)     
      implicit none      
      real*8 xi, eta, zeta, N(8)
      N(1)=.125D0*(1.D0-xi)*(1.D0-eta)*(1.D0-zeta)
      N(2)=.125D0*(1.D0+xi)*(1.D0-eta)*(1.D0-zeta)
      N(3)=.125D0*(1.D0+xi)*(1.D0+eta)*(1.D0-zeta)
      N(4)=.125D0*(1.D0-xi)*(1.D0+eta)*(1.D0-zeta)
      N(5)=.125D0*(1.D0-xi)*(1.D0-eta)*(1.D0+zeta)
      N(6)=.125D0*(1.D0+xi)*(1.D0-eta)*(1.D0+zeta)
      N(7)=.125D0*(1.D0+xi)*(1.D0+eta)*(1.D0+zeta)
      N(8)=.125D0*(1.D0-xi)*(1.D0+eta)*(1.D0+zeta)      
      return
      end

      subroutine CALC_SHAPE_FUNCTIONS_DERIV(xi, eta, zeta, dNdxi)      
      implicit none      
      real*8 xi, eta, zeta, dNdxi(3,8)
      dNdxi(1,1)=-0.125D0*(1.D0-eta)*(1.D0-zeta); dNdxi(2,1)=-0.125D0*(1.D0-xi)*(1.D0-zeta)
      dNdxi(3,1)=-0.125D0*(1.D0-xi)*(1.D0-eta); dNdxi(1,2)=0.125D0*(1.D0-eta)*(1.D0-zeta)
      dNdxi(2,2)=-0.125D0*(1.D0+xi)*(1.D0-zeta); dNdxi(3,2)=-0.125D0*(1.D0+xi)*(1.D0-eta)
      dNdxi(1,3)=0.125D0*(1.D0+eta)*(1.D0-zeta); dNdxi(2,3)=0.125D0*(1.D0+xi)*(1.D0-zeta)
      dNdxi(3,3)=-0.125D0*(1.D0+xi)*(1.D0+eta); dNdxi(1,4)=-0.125D0*(1.D0+eta)*(1.D0-zeta)
      dNdxi(2,4)=0.125D0*(1.D0-xi)*(1.D0-zeta); dNdxi(3,4)=-0.125D0*(1.D0-xi)*(1.D0+eta)
      dNdxi(1,5)=-0.125D0*(1.D0-eta)*(1.D0+zeta); dNdxi(2,5)=-0.125D0*(1.D0-xi)*(1.D0+zeta)
      dNdxi(3,5)=0.125D0*(1.D0-xi)*(1.D0-eta); dNdxi(1,6)=0.125D0*(1.D0-eta)*(1.D0+zeta)
      dNdxi(2,6)=-0.125D0*(1.D0+xi)*(1.D0+zeta); dNdxi(3,6)=0.125D0*(1.D0+xi)*(1.D0-eta)
      dNdxi(1,7)=0.125D0*(1.D0+eta)*(1.D0+zeta); dNdxi(2,7)=0.125D0*(1.D0+xi)*(1.D0+zeta)
      dNdxi(3,7)=0.125D0*(1.D0+xi)*(1.D0+eta); dNdxi(1,8)=-0.125D0*(1.D0+eta)*(1.D0+zeta)
      dNdxi(2,8)=0.125D0*(1.D0-xi)*(1.D0+zeta); dNdxi(3,8)=0.125D0*(1.D0-xi)*(1.D0+eta)
      return
      end

      SUBROUTINE JACOBIAN_FULL(COORDS, DN_DXI, JAC, DETJ)
      IMPLICIT NONE
      REAL*8, INTENT(IN) :: COORDS(8,3), DN_DXI(3,8)
      REAL*8, INTENT(OUT) :: JAC(3,3), DETJ
      JAC = matmul(DN_DXI, COORDS)
      JAC = transpose(JAC)
      DETJ = JAC(1,1)*(JAC(2,2)*JAC(3,3)-JAC(3,2)*JAC(2,3)) 
     1     - JAC(1,2)*(JAC(2,1)*JAC(3,3)-JAC(3,1)*JAC(2,3)) 
     2     + JAC(1,3)*(JAC(2,1)*JAC(3,2)-JAC(2,2)*JAC(3,1))
      RETURN
      END

      SUBROUTINE CALC_VOL_BBAR(B1I, X, V)
      ! Calculate element volume from B-bar formulation
      ! V = sum(X(i) * B1I(i)) where B1I is the first column of BiI
      IMPLICIT NONE
      REAL*8, INTENT(OUT) :: V
      REAL*8, INTENT(IN) :: B1I(8), X(8)
      INTEGER :: I
      V = 0.D0
      DO I=1,8
        V = V + X(I)*B1I(I)
      END DO
      RETURN
      END

      SUBROUTINE CALC_MISES_STRESS(stress, mises_stress)
      ! Calculate von Mises stress from stress tensor
      ! stress(1-6) = [σxx, σyy, σzz, σxy, σyz, σxz]
      ! von Mises = sqrt(1.5 * s_ij * s_ij) where s_ij is deviatoric stress
      IMPLICIT NONE
      REAL*8, INTENT(IN) :: stress(6)
      REAL*8, INTENT(OUT) :: mises_stress
      REAL*8 :: sxx, syy, szz, sxy, syz, sxz
      REAL*8 :: sxx_dev, syy_dev, szz_dev
      REAL*8 :: hydrostatic_pressure
      
      ! Extract stress components
      sxx = stress(1)
      syy = stress(2) 
      szz = stress(3)
      sxy = stress(4)
      syz = stress(5)
      sxz = stress(6)
      
      ! Calculate hydrostatic pressure
      hydrostatic_pressure = (sxx + syy + szz) / 3.0D0
      
      ! Calculate deviatoric stress components
      sxx_dev = sxx - hydrostatic_pressure
      syy_dev = syy - hydrostatic_pressure
      szz_dev = szz - hydrostatic_pressure
      
      ! Calculate von Mises stress
      ! von Mises = sqrt(1.5 * (sxx_dev^2 + syy_dev^2 + szz_dev^2 + 2*sxy^2 + 2*syz^2 + 2*sxz^2))
      mises_stress = sqrt(1.5D0 * (sxx_dev*sxx_dev + syy_dev*syy_dev + szz_dev*szz_dev + 
     &                             2.0D0*(sxy*sxy + syz*syz + sxz*sxz)))
      
      RETURN
      END

