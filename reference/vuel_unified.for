      module constants_module
      implicit none
      real*8, parameter :: one_over_eight = 1.0D0/8.0D0, WG=8.0D0
      
      ! ========== TUNING PARAMETERS ==========
      real*8, parameter :: SCALE_HOURGLASS = 1.0D0   ! Hourglass force scale
      real*8, parameter :: SCALE_K_MATRIX = 1.0D0    ! K matrix scale
      real*8, parameter :: SCALE_GAMMA = 1.0D0       ! Gamma vector scale
      real*8, parameter :: SCALE_C_TILDE = 1.0D0     ! C_tilde scale
      ! =======================================
            
      ! 修正后的沙漏形状向量 h
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
C     ================================================================
C     UNIFIED VUEL: C3D8R with Puso EAS Physical Stabilization
C     Supports both Linear (UL) and Hyperelastic (UL) constitutive laws
C     ================================================================
C
C     CONTROL FLAGS (via props - first 2 parameters):
C       props(1) = Material Model: 0=Linear (UL), 1=Hyperelastic (UL)
C       props(2) = Inversion Method: 0=Full (default), 1=Diagonal only
C
C     MATERIAL PROPERTIES (props array):
C       Linear model (props(1)=0) requires 5 properties total:
C         props(1) = 0   (model flag)
C         props(2) = 0/1 (inversion method)
C         props(3) = E   (Young's modulus)
C         props(4) = nu  (Poisson's ratio)
C         props(5) = rho (density)
C
C       Hyperelastic model (props(1)=1) requires 11 properties total:
C         props(1) = 1   (model flag)
C         props(2) = 0/1 (inversion method)
C         props(3) = C10 (1st order shear modulus)
C         props(4) = C20 (2nd order shear modulus)
C         props(5) = C30 (3rd order shear modulus)
C         props(6) = D1  (1st order compressibility)
C         props(7) = D2  (2nd order compressibility)
C         props(8) = D3  (3rd order compressibility)
C         props(9) = rho (density)
C         props(10) = E  (for hourglass control)
C         props(11) = nu (for hourglass control)
C
C     STATE VARIABLES (81 total, unified layout):
C       svars(1:6)   - Strain (both models: integrated strain)
C       svars(7:12)  - Cauchy stress (both models)
C       svars(13:24) - Hourglass forces (both models)
C       svars(25:48) - Old displacements (both models)
C       svars(49:57) - Deformation gradient F (Hyper only, incrementally updated)
C       svars(58:81) - Initial coordinates X0 (Hyper only, stored for initialization)
C
C     ================================================================
C
      use constants_module
    !   include 'vaba_param.inc'

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

C     ===== 统一变量声明 =====
      real*8 DMAT_CURRENT(6,6), FJAC(3,3), FJACINV(3,3), DETJ
      real*8 COORD(8,3), BiI(8,3), B(6,24)
      real*8 strain(6), dstrain(6), stress(6) 
      real*8 gammas(8,4), Velocity(8,3), fHG(24)
      real*8 shapes(8), mij, rowMass(ndofel), dudx(3,3)
      real*8 f_hg_old(3,4), df_hg(3,4), f_hg_new(3,4), C_tilde(6,6)
      real*8 dNdzeta(3,8), dt, VOL
      real*8 delta_u(24), u_old(24), Kmat(4,4,3,3), K_alpha_alpha(6,6)
      real*8 K_alpha_u(4,6,3), K_alpha_alpha_inv(6,6), mises_stress
      
C     ===== 材料模型选择 =====
      INTEGER :: MAT_MODEL
C     MAT_MODEL: 0 = Linear (UL), 1 = Hyperelastic (UL)
      
C     ===== 矩阵求逆方法选择 =====
      INTEGER :: INVERT_METHOD
C     INVERT_METHOD: 0 = Full Inversion (Default), 1 = Diagonal Only
      
C     ===== 并行输出控制 =====
      logical, save :: first_call_ever = .true.
      
C     ===== Linear模型专用变量 =====
      real*8 E_linear, nu_linear, rho_linear
      
C     ===== Hyperelastic模型专用变量 =====
      real*8 C10, C20, C30, D1, D2, D3, rho_hyper, E_hyper, nu_hyper
      real*8 F_old(3,3), F_new(3,3), I_mat(3,3), temp_mat(3,3)
      real*8 J_calc, W_dev_prime_calc
C     ===== Initial coordinates storage (for initialization only) =====
      real*8 COORD_X0(8,3), JAC_REF(3,3), JAC_CUR(3,3)
      real*8 JAC_REF_INV(3,3), DETJ_REF
C     ===== Hourglass control variables (now using current config for both models) =====
      real*8 BiI_0(8,3), VOL_0
C     ===== Mid-point F calculation variables (Hughes-Winget variant) =====
      real*8 COORD_OLD(8,3), COORD_MID(8,3), JAC_MID(3,3), DETJ_MID
      real*8 F_mid(3,3), BiI_mid(8,3), dL_mid(3,3), dD_mid(3,3), dW_mid(3,3)
      real*8 DeltaR(3,3), stress_old_matrix(3,3), stress_rotated_matrix(3,3)

C     Mass matrix calculation variables
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

      logical, save :: firstrun = .true.

C     ================================================================
C     INITIALIZATION AND VALIDATION
C     ================================================================
C     Only print info on the very first call to avoid parallel output chaos
      if(first_call_ever) then
          write(*,*) "========================================================"
          write(*,*) " UNIFIED VUEL C3D8R with Puso EAS Stabilization"
          write(*,*) " Paper: Int. J. Numer. Meth. Engng 2000; 49:1029-1064"
          write(*,*) "========================================================"
          write(*,*) " nblock  =", nblock
          write(*,*) " nnode   =", nnode
          write(*,*) " ndofel  =", ndofel
          write(*,*) " nsvars  =", nsvars
          write(*,*) " nprops  =", nprops
          write(*,*) ""
          
          ! 读取材料模型标志 (从props(1)读取)
          if (nprops < 2) then
              write(*,*) "ERROR: nprops < 2! Cannot read control flags."
              write(*,*) "Please define props(1) and props(2):"
              write(*,*) "  props(1) = Material Model"
              write(*,*) "             0 = Linear (uL)"
              write(*,*) "             1 = Hyperelastic (TL)"
              write(*,*) "  props(2) = Inversion Method"
              write(*,*) "             0 = Full (default)"
              write(*,*) "             1 = Diagonal only"
              call exit(1)
          endif
          
          MAT_MODEL = NINT(props(1))
          INVERT_METHOD = NINT(props(2))
          
          write(*,*) " Material Model (props(1)) =", MAT_MODEL
          write(*,*) " Inversion Method (props(2)) =", INVERT_METHOD
          write(*,*) ""
          
          if (MAT_MODEL .EQ. 0) then
              write(*,*) " >>> Selected: LINEAR (Updated Lagrangian)"
              write(*,*) " >>> Props requirement: 5 values"
              write(*,*) "     props(1) = 0   (model flag)"
              write(*,*) "     props(2) = 0/1 (inversion method)"
              write(*,*) "     props(3) = E   (Young's modulus)"
              write(*,*) "     props(4) = nu  (Poisson's ratio)"
              write(*,*) "     props(5) = rho (density)"
              if(nprops .lt. 5) then
                  write(*,*) "ERROR: Linear model requires 5 props!"
                  write(*,*) "Provided:", nprops
                  call exit(1)
              endif
              write(*,*) ""
              write(*,*) " >>> Loaded material parameters:"
              write(*,'(A,E15.8)') "     E   = ", props(3)
              write(*,'(A,E15.8)') "     nu  = ", props(4)
              write(*,'(A,E15.8)') "     rho = ", props(5)
          elseif (MAT_MODEL .EQ. 1) then
              write(*,*) " >>> Selected: HYPERELASTIC (Updated Lagrangian)"
              write(*,*) " >>> Props requirement: 11 values"
              write(*,*) "     props(1) = 1   (model flag)"
              write(*,*) "     props(2) = 0/1 (inversion method)"
              write(*,*) "     props(3-5) = C10, C20, C30 (shear moduli)"
              write(*,*) "     props(6-8) = D1, D2, D3 (compressibility)"
              write(*,*) "     props(9)   = rho (density)"
              write(*,*) "     props(10-11) = E, nu (for hourglass only)"
              if(nprops .lt. 11) then
                  write(*,*) "ERROR: Hyperelastic model requires 11 props!"
                  write(*,*) "Provided:", nprops
                  call exit(1)
              endif
              write(*,*) ""
              write(*,*) " >>> Loaded material parameters:"
              write(*,'(A,E15.8)') "     C10 = ", props(3)
              write(*,'(A,E15.8)') "     C20 = ", props(4)
              write(*,'(A,E15.8)') "     C30 = ", props(5)
              write(*,'(A,E15.8)') "     D1  = ", props(6)
              write(*,'(A,E15.8)') "     D2  = ", props(7)
              write(*,'(A,E15.8)') "     D3  = ", props(8)
              write(*,'(A,E15.8)') "     rho = ", props(9)
              write(*,'(A,E15.8)') "     E   = ", props(10)
              write(*,'(A,E15.8)') "     nu  = ", props(11)
          else
              write(*,*) "ERROR: Invalid material model flag!"
              write(*,*) "props(1) =", props(1)
              write(*,*) "Must be 0 (Linear) or 1 (Hyperelastic)"
              call exit(1)
          endif
          
          write(*,*) ""
          write(*,*) " Matrix Inversion Method:"
          if (INVERT_METHOD .EQ. 0) then
              write(*,*) "   0 = Full Inversion (Recommended for all materials)"
          else
              write(*,*) "   1 = Diagonal Only (Only for nu~0 materials)"
          endif
          write(*,*) ""
          write(*,*) " Required state variables: 81 (unified)"
          write(*,*) " (6 strain + 6 stress + 12 hourglass forces"
          write(*,*) "  + 24 old displacements + 9 deformation gradient F"
          write(*,*) "  + 24 initial coordinates X0)"
          if(nsvars .lt. 81) then
              write(*,*) ""
              write(*,*) "ERROR: Insufficient state variables!"
              write(*,*) "Required: 81, Provided:", nsvars
              write(*,*) "Please add to your input file:"
              write(*,*) "*USER OUTPUT VARIABLES"
              write(*,*) "81"
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
          first_call_ever = .false.
      endif

C     ================================================================
C     MATERIAL PROPERTIES LOADING (Dynamic based on MAT_MODEL)
C     ================================================================
      MAT_MODEL = NINT(props(1))
      INVERT_METHOD = NINT(props(2))
      
      if (MAT_MODEL .EQ. 0) then
          ! Linear model: props(3:5) = E, nu, rho
          E_linear = props(3)
          nu_linear = props(4)
          rho_linear = props(5)
      else
          ! Hyperelastic model: props(3:11) = C10, C20, C30, D1, D2, D3, rho, E, nu
          C10 = props(3)
          C20 = props(4)
          C30 = props(5)
          D1  = props(6)
          D2  = props(7)
          D3  = props(8)
          rho_hyper = props(9)
          E_hyper = props(10)
          nu_hyper = props(11)
      endif

C     ================================================================
C     MASS MATRIX CALCULATION (Unified - both models use same logic)
C     ================================================================
      if(lflags(iOpCode).eq.jMassCalc) then
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
                        call CALC_SHAPE_FUNCTIONS_DERIV(
     &                         xi(igauss), eta(igauss),zeta(igauss), dNdzeta)
                        call JACOBIAN_FULL(COORD, dNdzeta, FJAC, DETJ)

                        ! Use appropriate density
                        if (MAT_MODEL .EQ. 0) then
                            mij = w(igauss) * DETJ * rho_linear
                        else
                            mij = w(igauss) * DETJ * rho_hyper
                        endif
                        
                        do i = 1, nnode
                              do j = 1, nnode
                                amass(kblock,(i-1)*3+1, (j-1)*3+1) = 
     &                          amass(kblock,(i-1)*3+1, (j-1)*3+1) + mij*shapes(i)*shapes(j)
                                amass(kblock,(i-1)*3+2, (j-1)*3+2) = 
     &                          amass(kblock,(i-1)*3+2, (j-1)*3+2) + mij*shapes(i)*shapes(j)
                                amass(kblock,(i-1)*3+3, (j-1)*3+3) = 
     &                          amass(kblock,(i-1)*3+3, (j-1)*3+3) + mij*shapes(i)*shapes(j)
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
            
C     ================================================================
C     INTERNAL FORCE AND STABLE TIME STEP CALCULATION
C     ================================================================
      elseif(lflags(iOpCode).eq.jIntForceAndDtStable) then
            if(firstrun) write(*,*) "Starting internal force calculation..."

C           === Initialize state variables at Step 0, Inc 0 ===
            if(kstep.eq.0 .and. kinc.eq.0) then
                write(*,*) "Initializing state variables..."
                svars = 0.0D0
                ! 初始化 F = I 和存储初始坐标 X0 (仅用于 Hyperelastic 模型)
                if (MAT_MODEL .EQ. 1) then
                    write(*,*) "Initializing Deformation Gradient (F) to Identity."
                    write(*,*) "Storing initial coordinates X0 for total F."
                    do kblock_init = 1, nblock
                        ! F = I (identity)
                        svars(kblock_init, 49) = 1.0D0  ! F(1,1)
                        svars(kblock_init, 53) = 1.0D0  ! F(2,2)
                        svars(kblock_init, 57) = 1.0D0  ! F(3,3)
                        ! Store initial coordinates X0 in svars(58:81)
                        ! Layout: X0(node, coord) -> svars(58 + (node-1)*3 + (coord-1))
                        do J = 1, 8
                            do I = 1, 3
                                svars(kblock_init, 57 + (J-1)*3 + I) = 
     &                              coords(kblock_init, J, I)
                            enddo
                        enddo
                    enddo
                endif
                write(*,*) "State variables initialized."
            endif

            dt = dtimeCur
            
C           === Skip if dt=0 (initialization step) ===
            if (dt .lt. 1.0D-15) then
                if(firstrun) then
                    write(*,*) "dt=0 (initialization), skipping force calculation"
                endif
                rhs = 0.0D0
                firstrun = .false.
                RETURN
            endif

C           ================================================================
C           ELEMENT LOOP
C           ================================================================
            do kblock = 1,nblock
                  if(firstrun) write(*,*) "  Processing element block", kblock

C                 === Step 0: Common Geometry Calculation ===
                  if(firstrun) write(*,*) "  [Common] Extracting coordinates..."
                  do I=1,3
                        do J=1,8
                              COORD(J,I) = coords(kblock,J,I) + 
     &                              u(kblock,J*3-3+I)
                              Velocity(J,I) = v(kblock, (J-1)*3+I)
                        enddo
                  enddo

                  if(firstrun) write(*,*) "  [Common] Calculating B-bar..."
                  CALL CALC_B_BAR(BiI(1,1),COORD(1,2),COORD(1,3))
                  CALL CALC_B_BAR(BiI(1,2),COORD(1,3),COORD(1,1))
                  CALL CALC_B_BAR(BiI(1,3),COORD(1,1),COORD(1,2))

                  if(firstrun) write(*,*) "  [Common] Calculating volume..."
                  CALL CALC_VOL_BBAR(BiI(1,1),COORD(1,1),VOL)
                  BiI = BiI / VOL

                  if(firstrun) write(*,*) "  [Common] Calculating Jacobian..."
                  CALL JACOBIAN_CENTER(FJAC, COORD)
                  CALL JACOBIAN_INVERSE(FJAC, DETJ, FJACINV)

C                 === Step 1: Read state variables ===
                  strain = svars(kblock, 1:6)
                  stress = svars(kblock, 7:12)

C                 === Step 2: Compute velocity gradient L (dudx) ===
                  if(firstrun) write(*,*) "  [Common] Calculating L = du/dx..."
                  dudx = matmul(transpose(Velocity), BiI)

C                 ================================================================
C                 === DIVERGENCE POINT: CONSTITUTIVE UPDATE ===
C                 ================================================================
                  if (MAT_MODEL .EQ. 0) then
C                     ============================================================
C                     BRANCH A: LINEAR (Updated Lagrangian with Hughes-Winget rotation)
C                     Pure UL method using current configuration, but stress rotation
C                     uses Hughes-Winget method for objectivity
C                     ============================================================
                      if(firstrun) write(*,*) "  [Linear] UL with Hughes-Winget rotation..."
                      
C                     A1. Calculate constant DMAT (material properties)
                      CALL CALC_DDSDDE(DMAT_CURRENT, E_linear, nu_linear)
                      
C                     A2. Load old stress and convert to matrix form
                      stress_old_matrix(1,1) = svars(kblock, 7)
                      stress_old_matrix(2,2) = svars(kblock, 8)
                      stress_old_matrix(3,3) = svars(kblock, 9)
                      stress_old_matrix(1,2) = svars(kblock, 10)
                      stress_old_matrix(2,1) = svars(kblock, 10)
                      stress_old_matrix(2,3) = svars(kblock, 11)
                      stress_old_matrix(3,2) = svars(kblock, 11)
                      stress_old_matrix(1,3) = svars(kblock, 12)
                      stress_old_matrix(3,1) = svars(kblock, 12)
                      
C                     A3. Use current configuration velocity gradient (already calculated as dudx)
C                     dudx = L (velocity gradient at current configuration)
C                     Decompose into symmetric (D) and skew-symmetric (W) parts
                      dD_mid = 0.5D0 * (dudx + transpose(dudx))
                      dW_mid = 0.5D0 * (dudx - transpose(dudx))
                      
C                     A4. Calculate Hughes-Winget rotation increment (exact)
C                     DeltaR = (I - 0.5*DeltaW)^(-1) * (I + 0.5*DeltaW)
C                     where DeltaW = dW * dt
C                     Note: HUGHES_WINGET_ROTATION expects dL (full gradient), not just W
                      call HUGHES_WINGET_ROTATION(dudx, dt, DeltaR)
                      
C                     A5. Rotate old stress: sigma_rotated = DeltaR · sigma_old · DeltaR^T
                      stress_rotated_matrix = matmul(DeltaR, 
     &                    matmul(stress_old_matrix, transpose(DeltaR)))
                      
C                     A6. Convert rotated stress to Voigt notation
                      stress(1) = stress_rotated_matrix(1,1)
                      stress(2) = stress_rotated_matrix(2,2)
                      stress(3) = stress_rotated_matrix(3,3)
                      stress(4) = stress_rotated_matrix(1,2)
                      stress(5) = stress_rotated_matrix(2,3)
                      stress(6) = stress_rotated_matrix(1,3)
                      
C                     A7. Calculate strain increment from symmetric part: dstrain = D * dt
                      dstrain(1) = dD_mid(1,1) * dt
                      dstrain(2) = dD_mid(2,2) * dt
                      dstrain(3) = dD_mid(3,3) * dt
                      dstrain(4) = 2.0D0 * dD_mid(1,2) * dt
                      dstrain(5) = 2.0D0 * dD_mid(2,3) * dt
                      dstrain(6) = 2.0D0 * dD_mid(1,3) * dt
                      
C                     A8. Update stress: sigma_new = sigma_rotated + DMAT * dstrain
                      stress = stress + matmul(DMAT_CURRENT, dstrain)
                      
C                     A9. Update strain: strain_new = strain_old + dstrain
                      strain = strain + dstrain
                      
C                     A10. Store updated values
                      svars(kblock, 1:6) = strain
                      svars(kblock, 7:12) = stress
                      
                      if(firstrun) write(*,*) "  [Linear] UL with HW rotation completed."
                      
                  else
C                     ============================================================
C                     BRANCH B: HYPERELASTIC (Total Lagrangian - Full Formulation)
C                     Total stress calculation using end-of-step deformation gradient:
C                     1. Calculate F_new from current configuration (t+dt)
C                     2. Compute total Cauchy stress directly from F_new
C                     3. No incremental updates or stress rotation needed
C                     ============================================================
                      if(firstrun) write(*,*) "  [Hyper] Total formulation (full stress)..."
                      
C                     B1. Read initial coordinates X0 for TL hourglass control
                      do J = 1, 8
                          do I = 1, 3
                              COORD_X0(J, I) = svars(kblock, 57 + (J-1)*3 + I)
                          enddo
                      enddo
                      
C                     B2. Calculate B-bar based on initial configuration for TL hourglass
                      CALL CALC_B_BAR(BiI_0(1,1),COORD_X0(1,2),COORD_X0(1,3))
                      CALL CALC_B_BAR(BiI_0(1,2),COORD_X0(1,3),COORD_X0(1,1))
                      CALL CALC_B_BAR(BiI_0(1,3),COORD_X0(1,1),COORD_X0(1,2))
                      CALL CALC_VOL_BBAR(BiI_0(1,1),COORD_X0(1,1),VOL_0)
                      BiI_0 = BiI_0 / VOL_0
                      
C                     B3. Calculate reference Jacobian J0 = dX/d(xi) at center
                      CALL JACOBIAN_CENTER(JAC_REF, COORD_X0)
                      CALL JACOBIAN_INVERSE(JAC_REF, DETJ_REF, JAC_REF_INV)
                      
C                     B4. Calculate current Jacobian J_current = d(x)/d(xi) at center (t+dt)
C                     F_new = J_current * J0^(-1) where J_current is from COORD at t+dt
                      CALL JACOBIAN_CENTER(JAC_CUR, COORD)
                      F_new = matmul(JAC_CUR, JAC_REF_INV)
                      
C                     B5. Calculate PK-II stress from F_new (PK2 formulation)
                      block
                        real*8 :: F_loc(3,3)
                        real*8 :: stressPK2_voigt(6)
                        real*8 :: stressCauchy_voigt(6)
                        real*8 :: B_loc(3,3), B_bar_loc(3,3)
                        real*8 :: J_23, I1_loc
                        
                        F_loc = reshape(F_new, (/3,3/))
                        
C                       Calculate PK-II stress using N3 hyperelastic model
                        CALL CALC_HYPERELASTIC_STRESS_PK2_N3(F_loc,
     &                       C10, C20, C30, D1, D2, D3,
     &                       stressPK2_voigt,
     &                       J_calc, W_dev_prime_calc)
                        
C                       Also calculate Cauchy stress for post-processing
                        CALL CALC_HYPERELASTIC_STRESS_N3(F_loc,
     &                       C10, C20, C30, D1, D2, D3,
     &                       stressCauchy_voigt,
     &                       J_calc, W_dev_prime_calc)
                        
C                       Store PK-II stress for internal force calculation
                        stress = stressPK2_voigt
                        
C                       Store Cauchy stress for post-processing
                        svars(kblock, 7:12) = stressCauchy_voigt
                        
C                       Calculate kinematic quantities for DMAT
                        B_loc = matmul(F_loc, transpose(F_loc))
                        J_23 = J_calc**(-2.0D0/3.0D0)
                        B_bar_loc = J_23 * B_loc
                        I1_loc = B_bar_loc(1,1)+B_bar_loc(2,2)+B_bar_loc(3,3)
                        
C                       Calculate material tangent in PK2 framework
                        CALL CALC_EXACT_DMAT_PK2_N3(DMAT_CURRENT, F_loc, 
     &                      J_calc, B_bar_loc, I1_loc,
     &                      C10, C20, C30, D1, D2, D3)
                      end block
                      
C                     B6. Store updated F_new for next increment
                      svars(kblock, 49:57) = reshape(F_new, (/9/))
                      
                      if(firstrun) write(*,*) "  [Hyper] Total formulation update completed."
                  endif
C                 ================================================================
C                 === CONVERGENCE POINT: BOTH BRANCHES NOW HAVE stress AND DMAT_CURRENT ===
C                 ================================================================

C                 === Calculate Mises stress (diagnostic) ===
                  call CALC_MISES_STRESS(stress, mises_stress)
C                 Print Mises stress for every element at every increment
                  write(*,'(A,I0,A,I0,A,I0,A,E15.8)') "  [MISES] Step ", kstep, 
     &                    " Inc ", kinc, " Elem ", jElem(kblock), 
     &                    " Mises stress: ", mises_stress

C                 === Step 3: Internal Force Integration ===
                  if(firstrun) write(*,*) "  [Common] Computing internal force..."
C                 Use appropriate B-matrix and volume for each model
                  if (MAT_MODEL .EQ. 1) then
C                     === PURE TOTAL LAGRANGIAN FORCE INTEGRATION ===
C                     Current Status: 
C                       stress(1:6) holds PK2 stress (S) in Voigt
C                       svars(..., 49:57) holds Deformation Gradient (F)
C                       BiI_0 holds dN/dX (Reference Gradients)
C                       DETJ_REF is Reference Volume determinant
                      block
                        real*8 :: S_tensor(3,3), P_tensor(3,3), F_loc(3,3)
                        real*8 :: f_node(3)
                        integer :: n_idx, i_dof, j_coord
                        
                        ! 1. 还原 S (PK2) 为 3x3 张量
                        S_tensor(1,1)=stress(1); S_tensor(2,2)=stress(2); S_tensor(3,3)=stress(3)
                        S_tensor(1,2)=stress(4); S_tensor(2,1)=stress(4)
                        S_tensor(2,3)=stress(5); S_tensor(3,2)=stress(5)
                        S_tensor(1,3)=stress(6); S_tensor(3,1)=stress(6)
                        
                        ! 2. 获取 F (Deformation Gradient)
                        F_loc = reshape(svars(kblock, 49:57), (/3,3/))
                        
                        ! 3. 计算 P (PK1 Stress) = F * S
                        P_tensor = matmul(F_loc, S_tensor)
                        
                        ! 4. 积分: RHS = Sum( P * dN/dX ) * V0
                        !    Loop over nodes (8)
                        rhs(kblock, :) = 0.0D0
                        do n_idx = 1, 8
                           f_node = 0.0D0
                           ! f_i = Sum_j ( P_ij * dN/dX_j )
                           do i_dof = 1, 3
                              do j_coord = 1, 3
                                 ! P_tensor(i,j) * BiI_0(node, j)
                                 f_node(i_dof) = f_node(i_dof) + 
     &                                           P_tensor(i_dof, j_coord) * BiI_0(n_idx, j_coord)
                              enddo
                           enddo
                           
                           ! 组装到 RHS 全局矢量，并乘以体积系数
                           rhs(kblock, (n_idx-1)*3 + 1) = rhs(kblock, (n_idx-1)*3 + 1) + f_node(1) * DETJ_REF * WG
                           rhs(kblock, (n_idx-1)*3 + 2) = rhs(kblock, (n_idx-1)*3 + 2) + f_node(2) * DETJ_REF * WG
                           rhs(kblock, (n_idx-1)*3 + 3) = rhs(kblock, (n_idx-1)*3 + 3) + f_node(3) * DETJ_REF * WG
                        enddo
                      end block
                  else
C                     Linear: Use current B-matrix and current volume (Updated Lagrangian)
                      call FORM_B_MATRIX(BiI, B)
                      rhs(kblock,:) = matmul(transpose(B), stress) * DETJ * WG
                  endif

C                 Only print detailed RHS for first element at selected increments
                  if(firstrun .and. kblock .eq. 1) then
                      write(*,'(A,I0,A,I0,A)') "  [RHS_OUTPUT] Step ", kstep, 
     &                    " Inc ", kinc, " - Initial RHS (before hourglass):"
                      do I=1,24
                          write(*,'(A,I2,A,E15.8)') "    DOF ", I, 
     &                        ": ", rhs(kblock,I)
                      enddo
                  endif

C                 ================================================================
C                 === Step 4: UNIFIED HOURGLASS CONTROL ===
C                 === Using Hyper version (more robust, works for both models) ===
C                 ================================================================
                  
                  if(firstrun) write(*,*) "  [Hourglass] Calculating gammas..."
C                 Mixed formulation: UL constitutive + TL hourglass for Hyperelastic
                  if (MAT_MODEL .EQ. 1) then
C                     Hyperelastic: Use initial configuration for hourglass (TL)
                      call HOURGLASS_SHAPE_VECTORS(BiI_0, COORD_X0, H_VECTORS, gammas)
                  else
C                     Linear: Use current configuration (UL)
                      call HOURGLASS_SHAPE_VECTORS(BiI, COORD, H_VECTORS, gammas)
                  endif
                  
                  if(firstrun) write(*,*) "  [Hourglass] Calculating Cmtxh..."
C                 Mixed formulation: UL constitutive + TL hourglass for Hyperelastic
                  if (MAT_MODEL .EQ. 1) then
C                     Hyperelastic: Use initial Jacobian for hourglass (TL)
C                     DMAT_CURRENT is from current F, but hourglass uses initial geometry
                      call GET_CMTXH(DMAT_CURRENT, JAC_REF, DETJ_REF, C_tilde)
                  else
C                     Linear: Use current Jacobian (UL)
                      call GET_CMTXH(DMAT_CURRENT, FJAC, DETJ, C_tilde)
                  endif
                  
                  u_old = svars(kblock, 25:48)
                  do I=1,24
                      delta_u(I) = u(kblock,I) - u_old(I)
                  enddo
                  
                  if(firstrun) write(*,*) "  [Hourglass] Calculating K matrices..."
C                 Mixed formulation: UL constitutive + TL hourglass for Hyperelastic
                  if (MAT_MODEL .EQ. 1) then
C                     Hyperelastic: Use initial volume for hourglass (TL)
                      call CALC_K_MATRICES(C_tilde, DETJ_REF*WG, Kmat, 
     &                                     K_alpha_u, K_alpha_alpha)
                  else
C                     Linear: Use current volume (UL)
                      call CALC_K_MATRICES(C_tilde, DETJ*WG, Kmat, 
     &                                     K_alpha_u, K_alpha_alpha)
                  endif
                  
                  if(firstrun) write(*,*) "  [Hourglass] Inverting K_alpha_alpha..."
                  if (INVERT_METHOD .EQ. 0) then
                      call INVERT_6X6_FULL(K_alpha_alpha, K_alpha_alpha_inv)
                  else
                      call INVERT_6X6_DIAGONAL(K_alpha_alpha, K_alpha_alpha_inv)
                  endif
                  
                  if(firstrun) write(*,*) "  [Hourglass] Calculating df_hg..."
C                 Mixed formulation: UL constitutive + TL hourglass for Hyperelastic
                  if (MAT_MODEL .EQ. 1) then
C                     Hyperelastic: Use initial Jacobian for hourglass (TL)
                      call CALC_DF_HG_EAS(Kmat, K_alpha_u, K_alpha_alpha_inv,
     &                                    gammas, delta_u, transpose(JAC_REF), 
     &                                    df_hg)
                  else
C                     Linear: Use current Jacobian (UL)
                      call CALC_DF_HG_EAS(Kmat, K_alpha_u, K_alpha_alpha_inv,
     &                                    gammas, delta_u, transpose(FJAC), 
     &                                    df_hg)
                  endif

                  if(firstrun) write(*,*) "  [Hourglass] Updating hourglass forces..."
                  do I=1,4
                      f_hg_old(:,I) = svars(kblock, 12+(I-1)*3+1:12+(I-1)*3+3)
                  enddo
                  f_hg_new = f_hg_old + df_hg
                  do I=1,4
                      svars(kblock, 12+(I-1)*3+1:12+(I-1)*3+3) = f_hg_new(:,I)
                  enddo
                  
C                 Only print hourglass details for first element at selected increments
                  if(firstrun .and. kblock .eq. 1) then
                      write(*,'(A,I0,A,I0,A)') "  [FHG_DETAILS] Step ", kstep, 
     &                    " Inc ", kinc, " - Hourglass force details:"
                      do I=1,4
                          write(*,'(A,I1,A,3E15.8)') "    f_hg_new(", I, "): ", 
     &                        f_hg_new(1,I), f_hg_new(2,I), f_hg_new(3,I)
                      enddo
                  endif
                  
                  if(firstrun) write(*,*) "  [Hourglass] Calculating F_stab..."
                  fHG = 0.0D0
C                 Mixed formulation: UL constitutive + TL hourglass for Hyperelastic
                  if (MAT_MODEL .EQ. 1) then
C                     Hyperelastic: Use initial Jacobian for hourglass (TL)
                      do I = 1, 4
                          do J = 1, 8
                              fHG(J*3-2)=fHG(J*3-2)+gammas(J,I)*(
     *                        JAC_REF(1,1)*f_hg_new(1,I)+JAC_REF(1,2)*f_hg_new(2,I)
     *                        +JAC_REF(1,3)*f_hg_new(3,I))
                              fHG(J*3-1)=fHG(J*3-1)+gammas(J,I)*(
     *                        JAC_REF(2,1)*f_hg_new(1,I)+JAC_REF(2,2)*f_hg_new(2,I)
     *                        +JAC_REF(2,3)*f_hg_new(3,I))
                              fHG(J*3-0)=fHG(J*3-0)+gammas(J,I)*(
     *                        JAC_REF(3,1)*f_hg_new(1,I)+JAC_REF(3,2)*f_hg_new(2,I)
     *                        +JAC_REF(3,3)*f_hg_new(3,I))
                          enddo
                      enddo
                      fHG = fHG * (DETJ_REF*WG / 8.0D0) * SCALE_HOURGLASS
                  else
C                     Linear: Use current Jacobian (UL)
                      do I = 1, 4
                          do J = 1, 8
                              fHG(J*3-2)=fHG(J*3-2)+gammas(J,I)*(
     *                        FJAC(1,1)*f_hg_new(1,I)+FJAC(1,2)*f_hg_new(2,I)
     *                        +FJAC(1,3)*f_hg_new(3,I))
                              fHG(J*3-1)=fHG(J*3-1)+gammas(J,I)*(
     *                        FJAC(2,1)*f_hg_new(1,I)+FJAC(2,2)*f_hg_new(2,I)
     *                        +FJAC(2,3)*f_hg_new(3,I))
                              fHG(J*3-0)=fHG(J*3-0)+gammas(J,I)*(
     *                        FJAC(3,1)*f_hg_new(1,I)+FJAC(3,2)*f_hg_new(2,I)
     *                        +FJAC(3,3)*f_hg_new(3,I))
                          enddo
                      enddo
                      fHG = fHG * (DETJ*WG / 8.0D0) * SCALE_HOURGLASS
                  endif
                  
C                 Only print FHG and final RHS for first element at selected increments
                  if(firstrun .and. kblock .eq. 1) then
                      write(*,'(A,I0,A,I0,A)') "  [FHG_OUTPUT] Step ", kstep, 
     &                    " Inc ", kinc, " - Hourglass force FHG:"
                      do I=1,24
                          write(*,'(A,I2,A,E15.8)') "    DOF ", I, 
     &                        ": ", fHG(I)
                      enddo
                  endif
                  
                  if(firstrun) write(*,*) "  [Hourglass] Adding F_stab to RHS..."
                  rhs(kblock,:) = rhs(kblock,:) + fHG
                  
                  if(firstrun .and. kblock .eq. 1) then
                      write(*,'(A,I0,A,I0,A)') "  [RHS_FINAL] Step ", kstep, 
     &                    " Inc ", kinc, " - Final RHS (after hourglass):"
                      do I=1,24
                          write(*,'(A,I2,A,E15.8)') "    DOF ", I, 
     &                        ": ", rhs(kblock,I)
                      enddo
                  endif
                  
                  svars(kblock, 25:48) = u(kblock,:)

            end do
            if(firstrun) write(*,*) "Internal force calculation completed."
      endif

      firstrun = .false.
      RETURN
      END

C     ================================================================
C     SUBROUTINES COMMON TO BOTH MODELS
C     ================================================================

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

      SUBROUTINE INVERT_3X3(A, AINV)
C     ------------------------------------------------------------------
C     Invert a 3x3 matrix using explicit formula
C     ------------------------------------------------------------------
      IMPLICIT NONE
      REAL*8, INTENT(IN)  :: A(3,3)
      REAL*8, INTENT(OUT) :: AINV(3,3)
      REAL*8 :: DET, DET_INV
      
      DET = A(1,1)*(A(2,2)*A(3,3)-A(2,3)*A(3,2)) -
     1      A(1,2)*(A(2,1)*A(3,3)-A(2,3)*A(3,1)) +
     2      A(1,3)*(A(2,1)*A(3,2)-A(2,2)*A(3,1))
      
      IF (ABS(DET) .LT. 1.0D-20) THEN
        write(*,*) "ERROR: 3x3 matrix determinant is zero or too small!"
        write(*,*) "Determinant = ", DET
        STOP
      END IF
      
      DET_INV = 1.0D0 / DET
      
      AINV(1,1) = (A(2,2)*A(3,3)-A(2,3)*A(3,2)) * DET_INV
      AINV(1,2) = (A(1,3)*A(3,2)-A(1,2)*A(3,3)) * DET_INV
      AINV(1,3) = (A(1,2)*A(2,3)-A(1,3)*A(2,2)) * DET_INV
      AINV(2,1) = (A(2,3)*A(3,1)-A(2,1)*A(3,3)) * DET_INV
      AINV(2,2) = (A(1,1)*A(3,3)-A(1,3)*A(3,1)) * DET_INV
      AINV(2,3) = (A(1,3)*A(2,1)-A(1,1)*A(2,3)) * DET_INV
      AINV(3,1) = (A(2,1)*A(3,2)-A(2,2)*A(3,1)) * DET_INV
      AINV(3,2) = (A(1,2)*A(3,1)-A(1,1)*A(3,2)) * DET_INV
      AINV(3,3) = (A(1,1)*A(2,2)-A(1,2)*A(2,1)) * DET_INV
      
      RETURN
      END

      SUBROUTINE HUGHES_WINGET_ROTATION(dL, dt, DeltaR)
C     ------------------------------------------------------------------
C     Calculate Hughes-Winget rotation increment:
C     DeltaR = (I - 0.5*DeltaW)^(-1) * (I + 0.5*DeltaW)
C     where DeltaW = asym(dL) = 0.5 * (dL - dL^T)
C     ------------------------------------------------------------------
      IMPLICIT NONE
      REAL*8, INTENT(IN)  :: dL(3,3), dt
      REAL*8, INTENT(OUT) :: DeltaR(3,3)
      REAL*8 :: DeltaW(3,3), I_minus_half_DeltaW(3,3)
      REAL*8 :: I_plus_half_DeltaW(3,3), I_minus_half_DeltaW_inv(3,3)
      REAL*8 :: I_mat(3,3)
      INTEGER :: i, j
      
C     Initialize identity matrix
      I_mat = 0.0D0
      do i = 1, 3
          I_mat(i,i) = 1.0D0
      enddo
      
C     Calculate skew-symmetric part: DeltaW = 0.5 * (dL - dL^T) * dt
      DeltaW = 0.5D0 * (dL - transpose(dL)) * dt
      
C     Calculate I - 0.5*DeltaW
      I_minus_half_DeltaW = I_mat - 0.5D0 * DeltaW
      
C     Calculate I + 0.5*DeltaW
      I_plus_half_DeltaW = I_mat + 0.5D0 * DeltaW
      
C     Invert (I - 0.5*DeltaW)
      call INVERT_3X3(I_minus_half_DeltaW, I_minus_half_DeltaW_inv)
      
C     Calculate DeltaR = (I - 0.5*DeltaW)^(-1) * (I + 0.5*DeltaW)
      DeltaR = matmul(I_minus_half_DeltaW_inv, I_plus_half_DeltaW)
      
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
      do i = 1, 4
        h_dot_x = 0.0D0
        do A = 1, 8
          h_dot_x(1) = h_dot_x(1) + h(A,i) * COORD(A,1)
          h_dot_x(2) = h_dot_x(2) + h(A,i) * COORD(A,2)
          h_dot_x(3) = h_dot_x(3) + h(A,i) * COORD(A,3)
        enddo
        do A = 1, 8
          gammas(A,i) = SCALE_GAMMA * one_over_eight * (h(A,i) - 
     &                  (h_dot_x(1)*BiI(A,1) + h_dot_x(2)*BiI(A,2) + 
     &                   h_dot_x(3)*BiI(A,3)))
        enddo
      enddo
      RETURN
      END

      SUBROUTINE CALC_MISES_STRESS(stress, mises_stress)
      IMPLICIT NONE
      REAL*8, INTENT(IN) :: stress(6)
      REAL*8, INTENT(OUT) :: mises_stress
      REAL*8 :: sxx, syy, szz, sxy, syz, sxz
      REAL*8 :: sxx_dev, syy_dev, szz_dev, hydrostatic_pressure
      sxx = stress(1); syy = stress(2); szz = stress(3)
      sxy = stress(4); syz = stress(5); sxz = stress(6)
      hydrostatic_pressure = (sxx + syy + szz) / 3.0D0
      sxx_dev = sxx - hydrostatic_pressure
      syy_dev = syy - hydrostatic_pressure
      szz_dev = szz - hydrostatic_pressure
      mises_stress = sqrt(1.5D0 * (sxx_dev*sxx_dev + syy_dev*syy_dev + 
     &       szz_dev*szz_dev + 2.0D0*(sxy*sxy + syz*syz + sxz*sxz)))
      RETURN
      END

      SUBROUTINE CALC_VOL_BBAR(B1I, X, V)
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
      dNdxi(1,1)=-0.125D0*(1.D0-eta)*(1.D0-zeta)
      dNdxi(2,1)=-0.125D0*(1.D0-xi)*(1.D0-zeta)
      dNdxi(3,1)=-0.125D0*(1.D0-xi)*(1.D0-eta)
      dNdxi(1,2)=0.125D0*(1.D0-eta)*(1.D0-zeta)
      dNdxi(2,2)=-0.125D0*(1.D0+xi)*(1.D0-zeta)
      dNdxi(3,2)=-0.125D0*(1.D0+xi)*(1.D0-eta)
      dNdxi(1,3)=0.125D0*(1.D0+eta)*(1.D0-zeta)
      dNdxi(2,3)=0.125D0*(1.D0+xi)*(1.D0-zeta)
      dNdxi(3,3)=-0.125D0*(1.D0+xi)*(1.D0+eta)
      dNdxi(1,4)=-0.125D0*(1.D0+eta)*(1.D0-zeta)
      dNdxi(2,4)=0.125D0*(1.D0-xi)*(1.D0-zeta)
      dNdxi(3,4)=-0.125D0*(1.D0-xi)*(1.D0+eta)
      dNdxi(1,5)=-0.125D0*(1.D0-eta)*(1.D0+zeta)
      dNdxi(2,5)=-0.125D0*(1.D0-xi)*(1.D0+zeta)
      dNdxi(3,5)=0.125D0*(1.D0-xi)*(1.D0-eta)
      dNdxi(1,6)=0.125D0*(1.D0-eta)*(1.D0+zeta)
      dNdxi(2,6)=-0.125D0*(1.D0+xi)*(1.D0+zeta)
      dNdxi(3,6)=0.125D0*(1.D0+xi)*(1.D0-eta)
      dNdxi(1,7)=0.125D0*(1.D0+eta)*(1.D0+zeta)
      dNdxi(2,7)=0.125D0*(1.D0+xi)*(1.D0+zeta)
      dNdxi(3,7)=0.125D0*(1.D0+xi)*(1.D0+eta)
      dNdxi(1,8)=-0.125D0*(1.D0+eta)*(1.D0+zeta)
      dNdxi(2,8)=0.125D0*(1.D0-xi)*(1.D0+zeta)
      dNdxi(3,8)=0.125D0*(1.D0-xi)*(1.D0+eta)
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

C     ================================================================
C     SUBROUTINES FOR LINEAR MODEL (Branch A)
C     ================================================================

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

C     ================================================================
C     SUBROUTINES FOR HYPERELASTIC MODEL (Branch B)
C     ================================================================

      SUBROUTINE CALC_HYPERELASTIC_STRESS_N3(F,
     &     C10, C20, C30, D1, D2, D3,
     &     stress_voigt,
     &     J_out, W_dev_prime_out)
      IMPLICIT NONE
      REAL*8, INTENT(IN) :: F(3,3)
      REAL*8, INTENT(IN) :: C10, C20, C30, D1, D2, D3
      REAL*8, INTENT(OUT) :: stress_voigt(6)
      REAL*8, INTENT(OUT) :: J_out, W_dev_prime_out
      
      REAL*8 :: B(3,3), B_bar(3,3), B_bar_dev(3,3)
      REAL*8 :: sigma_matrix(3,3), I_mat_local(3,3)
      REAL*8 :: J, J_minus_2_3, I1_bar, I1_bar_minus_3
      REAL*8 :: W_dev_prime, pressure, J_minus_1
      
      J = F(1,1)*(F(2,2)*F(3,3)-F(2,3)*F(3,2)) -
     &    F(1,2)*(F(2,1)*F(3,3)-F(2,3)*F(3,1)) +
     &    F(1,3)*(F(2,1)*F(3,2)-F(2,2)*F(3,1))
      
      if (J <= 1.0D-10) then
          write(*,*) "ERROR: J <= 0 in stress calculation!"
          call exit(1)
      endif
      
      B = matmul(F, transpose(F))
      J_minus_2_3 = J**(-2.0D0/3.0D0)
      B_bar = J_minus_2_3 * B
      I1_bar = B_bar(1,1) + B_bar(2,2) + B_bar(3,3)
      I1_bar_minus_3 = I1_bar - 3.0D0
      
      W_dev_prime = C10 + 2.0D0 * C20 * I1_bar_minus_3 +
     &              3.0D0 * C30 * I1_bar_minus_3**2.0D0
      
      J_minus_1 = J - 1.0D0
      pressure = 2.0D0 * (1.0D0 / D1) * J_minus_1
      if (D2 > 1.0D-20) then
          pressure = pressure + 4.0D0 * (1.0D0 / D2) * J_minus_1**3.0D0
      endif
      if (D3 > 1.0D-20) then
          pressure = pressure + 6.0D0 * (1.0D0 / D3) * J_minus_1**5.0D0
      endif
      
      I_mat_local = 0.0D0
      I_mat_local(1,1) = 1.0D0
      I_mat_local(2,2) = 1.0D0
      I_mat_local(3,3) = 1.0D0
      
      B_bar_dev = B_bar - (I1_bar / 3.0D0) * I_mat_local
      sigma_matrix = (2.0D0 / J) * (W_dev_prime * B_bar_dev) +
     &               (pressure * I_mat_local)
                     
      stress_voigt(1) = sigma_matrix(1,1)
      stress_voigt(2) = sigma_matrix(2,2)
      stress_voigt(3) = sigma_matrix(3,3)
      stress_voigt(4) = sigma_matrix(1,2)
      stress_voigt(5) = sigma_matrix(2,3)
      stress_voigt(6) = sigma_matrix(1,3)

      J_out = J
      W_dev_prime_out = W_dev_prime

      RETURN
      END

      SUBROUTINE CALC_HYPERELASTIC_STRESS_PK2_N3(F,
     &     C10, C20, C30, D1, D2, D3,
     &     stressPK2_voigt,
     &     J_out, W_dev_prime_out)
C     ------------------------------------------------------------------
C     Calculate PK-II stress for N3 hyperelastic model
C     Based on Total Lagrangian formulation
C     Reference: Similar to uel_nlmech_pk2.for but extended to N3 form
C     ------------------------------------------------------------------
      IMPLICIT NONE
      REAL*8, INTENT(IN) :: F(3,3)
      REAL*8, INTENT(IN) :: C10, C20, C30, D1, D2, D3
      REAL*8, INTENT(OUT) :: stressPK2_voigt(6)
      REAL*8, INTENT(OUT) :: J_out, W_dev_prime_out
      
      REAL*8 :: C(3,3), Cinv(3,3), C_bar(3,3)
      REAL*8 :: S_matrix(3,3), I_mat_local(3,3)
      REAL*8 :: J, J_minus_2_3, I1_bar, I1_bar_minus_3
      REAL*8 :: W_dev_prime, p_vol, J_minus_1
      
C     Calculate J = det(F)
      J = F(1,1)*(F(2,2)*F(3,3)-F(2,3)*F(3,2)) -
     &    F(1,2)*(F(2,1)*F(3,3)-F(2,3)*F(3,1)) +
     &    F(1,3)*(F(2,1)*F(3,2)-F(2,2)*F(3,1))
      
      if (J <= 1.0D-10) then
          write(*,*) "ERROR: J <= 0 in PK2 stress calculation!"
          call exit(1)
      endif
      
C     Calculate right Cauchy-Green tensor: C = F^T * F
      C = matmul(transpose(F), F)
      
C     Calculate inverse of C
      call INVERT_3X3(C, Cinv)
      
C     Calculate isochoric part: C_bar = J^(-2/3) * C
      J_minus_2_3 = J**(-2.0D0/3.0D0)
      C_bar = J_minus_2_3 * C
      
C     Calculate first invariant: I1_bar = tr(C_bar)
      I1_bar = C_bar(1,1) + C_bar(2,2) + C_bar(3,3)
      I1_bar_minus_3 = I1_bar - 3.0D0
      
C     Calculate derivative of deviatoric strain energy: W_dev_prime
      W_dev_prime = C10 + 2.0D0 * C20 * I1_bar_minus_3 +
     &              3.0D0 * C30 * I1_bar_minus_3**2.0D0
      
C     Calculate volumetric pressure term
      J_minus_1 = J - 1.0D0
      p_vol = 2.0D0 * (1.0D0 / D1) * J_minus_1
      if (D2 > 1.0D-20) then
          p_vol = p_vol + 4.0D0 * (1.0D0 / D2) * J_minus_1**3.0D0
      endif
      if (D3 > 1.0D-20) then
          p_vol = p_vol + 6.0D0 * (1.0D0 / D3) * J_minus_1**5.0D0
      endif
      
C     Initialize identity matrix
      I_mat_local = 0.0D0
      I_mat_local(1,1) = 1.0D0
      I_mat_local(2,2) = 1.0D0
      I_mat_local(3,3) = 1.0D0
      
C     Calculate PK-II stress: S = 2 * dW/dC
C     For deviatoric part: S_dev = 2 * W_dev_prime * (dI1_bar/dC)
C     dI1_bar/dC = J^(-2/3) * (I - (1/3)*I1_bar*C^(-1))
C     For volumetric part: S_vol = p_vol * J * C^(-1)
C     where p_vol = 2 * dW_vol/dJ (already calculated above)
      S_matrix = 2.0D0 * J_minus_2_3 * W_dev_prime * 
     &           (I_mat_local - (I1_bar / 3.0D0) * Cinv) +
     &           p_vol * J * Cinv
      
C     Convert to Voigt notation
      stressPK2_voigt(1) = S_matrix(1,1)
      stressPK2_voigt(2) = S_matrix(2,2)
      stressPK2_voigt(3) = S_matrix(3,3)
      stressPK2_voigt(4) = S_matrix(1,2)
      stressPK2_voigt(5) = S_matrix(2,3)
      stressPK2_voigt(6) = S_matrix(1,3)

      J_out = J
      W_dev_prime_out = W_dev_prime

      RETURN
      END

      SUBROUTINE CALC_EXACT_DMAT_N3(DMAT, F, J, B_bar, I1_bar,
     &                              C10, C20, C30, D1, D2, D3)
      IMPLICIT NONE
      REAL*8, INTENT(OUT) :: DMAT(6,6)
      REAL*8, INTENT(IN)  :: F(3,3), B_bar(3,3), J, I1_bar
      REAL*8, INTENT(IN)  :: C10, C20, C30, D1, D2, D3

      REAL*8 :: I_den(3,3), dev_b_bar(3,3)
      REAL*8 :: W1, W2, W11, p, p_tilde
      REAL*8 :: c_vol(6,6), c_dev(6,6)
      REAL*8 :: coeff1, coeff2, term1, term2
      REAL*8 :: two_over_J, two_over_3, four_over_3, J_minus_1
      INTEGER :: i, j_tensor, k, l, m, n
      
      INTEGER :: map(6,2)
      DATA map / 1,1, 2,2, 3,3, 1,2, 2,3, 1,3 /

      DMAT = 0.0D0
      I_den = 0.0D0
      I_den(1,1)=1.0D0; I_den(2,2)=1.0D0; I_den(3,3)=1.0D0

      W1 = C10 + 2.0D0*C20*(I1_bar-3.0D0) + 
     &     3.0D0*C30*(I1_bar-3.0D0)**2.0D0
      W11 = 2.0D0*C20 + 6.0D0*C30*(I1_bar-3.0D0)

      J_minus_1 = J - 1.0D0
      p = 2.0D0/D1 * J_minus_1
      if (D2 > 1.0D-20) then
          p = p + 4.0D0/D2 * J_minus_1**3.0D0
      endif
      if (D3 > 1.0D-20) then
          p = p + 6.0D0/D3 * J_minus_1**5.0D0
      endif
      
      p_tilde = p + J * 2.0D0/D1
      if (D2 > 1.0D-20) then
          p_tilde = p_tilde + J * 12.0D0/D2 * J_minus_1**2.0D0
      endif
      if (D3 > 1.0D-20) then
          p_tilde = p_tilde + J * 30.0D0/D3 * J_minus_1**4.0D0
      endif

      dev_b_bar = B_bar - (I1_bar/3.0D0) * I_den

      two_over_J = 2.0D0 / J
      two_over_3 = 2.0D0 / 3.0D0
      four_over_3 = 4.0D0 / 3.0D0

      DO m = 1, 6
        DO n = 1, 6
          i = map(m,1); j_tensor = map(m,2)
          k = map(n,1); l = map(n,2)
          
          term1 = 0.0D0
          if (i.eq.j_tensor .and. k.eq.l) term1 = 1.0D0
          
          term2 = 0.0D0
          if (i.eq.k .and. j_tensor.eq.l) term2 = term2 + 0.5D0
          if (i.eq.l .and. j_tensor.eq.k) term2 = term2 + 0.5D0
          
          c_vol(m,n) = p_tilde * term1 - 2.0D0 * p * term2
          
          coeff1 = two_over_J * (W1 + I1_bar * W11)
          coeff2 = two_over_J * W11
          
          c_dev(m,n) = 0.0D0
          
          if (i.eq.j_tensor .and. k.eq.l) then
             c_dev(m,n) = c_dev(m,n) + coeff1 * two_over_3
          endif
          
          if (i.eq.j_tensor) c_dev(m,n) = c_dev(m,n) - coeff1 * B_bar(k,l)
          if (k.eq.l) c_dev(m,n) = c_dev(m,n) - coeff1 * B_bar(i,j_tensor)
          
          c_dev(m,n) = c_dev(m,n) + coeff2 * B_bar(i,j_tensor) * B_bar(k,l)
          
          term2 = 0.0D0
          if (i.eq.k) term2 = term2 + B_bar(j_tensor,l)
          if (j_tensor.eq.l) term2 = term2 + B_bar(i,k)
          if (i.eq.l) term2 = term2 + B_bar(j_tensor,k)
          if (j_tensor.eq.k) term2 = term2 + B_bar(i,l)
          
          c_dev(m,n) = c_dev(m,n) + (two_over_J * W1) * 0.5D0 * term2
          
          DMAT(m,n) = c_vol(m,n) + c_dev(m,n)
          
        ENDDO
      ENDDO

      RETURN
      END

      SUBROUTINE CALC_EXACT_DMAT_PK2_N3(DMAT, F, J, B_bar, I1_bar,
     &                                  C10, C20, C30, D1, D2, D3)
C     ------------------------------------------------------------------
C     Calculate material tangent matrix dS/dC for PK-II stress
C     Based on Total Lagrangian formulation
C     Reference: Similar to uel_nlmech_pk2.for but extended to N3 form
C     ------------------------------------------------------------------
      IMPLICIT NONE
      REAL*8, INTENT(OUT) :: DMAT(6,6)
      REAL*8, INTENT(IN)  :: F(3,3), B_bar(3,3), J, I1_bar
      REAL*8, INTENT(IN)  :: C10, C20, C30, D1, D2, D3

      REAL*8 :: C(3,3), Cinv(3,3), C_bar(3,3)
      REAL*8 :: I_den(3,3)
      REAL*8 :: W1, W11, p, p_tilde
      REAL*8 :: c_vol(6,6), c_dev(6,6)
      REAL*8 :: coeff1, coeff2, term1, term2
      REAL*8 :: J_minus_2_3, J_minus_1
      INTEGER :: i, j_tensor, k, l, m, n
      
      INTEGER :: map(6,2)
      DATA map / 1,1, 2,2, 3,3, 1,2, 2,3, 1,3 /

      DMAT = 0.0D0
      I_den = 0.0D0
      I_den(1,1)=1.0D0; I_den(2,2)=1.0D0; I_den(3,3)=1.0D0

C     Calculate C and Cinv
      C = matmul(transpose(F), F)
      call INVERT_3X3(C, Cinv)
      
C     Calculate C_bar = J^(-2/3) * C
      J_minus_2_3 = J**(-2.0D0/3.0D0)
      C_bar = J_minus_2_3 * C

C     Calculate strain energy derivatives
      W1 = C10 + 2.0D0*C20*(I1_bar-3.0D0) + 
     &     3.0D0*C30*(I1_bar-3.0D0)**2.0D0
      W11 = 2.0D0*C20 + 6.0D0*C30*(I1_bar-3.0D0)

C     Calculate volumetric pressure and its derivative
      J_minus_1 = J - 1.0D0
      p = 2.0D0/D1 * J_minus_1
      if (D2 > 1.0D-20) then
          p = p + 4.0D0/D2 * J_minus_1**3.0D0
      endif
      if (D3 > 1.0D-20) then
          p = p + 6.0D0/D3 * J_minus_1**5.0D0
      endif
      
      p_tilde = p + J * 2.0D0/D1
      if (D2 > 1.0D-20) then
          p_tilde = p_tilde + J * 12.0D0/D2 * J_minus_1**2.0D0
      endif
      if (D3 > 1.0D-20) then
          p_tilde = p_tilde + J * 30.0D0/D3 * J_minus_1**4.0D0
      endif

C     Calculate material tangent: C_ijkl = dS_ij/dC_kl
C     For PK-II stress, this is computed in reference configuration
      DO m = 1, 6
        DO n = 1, 6
          i = map(m,1); j_tensor = map(m,2)
          k = map(n,1); l = map(n,2)
          
C         Volumetric part: dS_vol/dC
          term1 = 0.0D0
          if (i.eq.j_tensor .and. k.eq.l) term1 = 1.0D0
          
          term2 = 0.0D0
          if (i.eq.k .and. j_tensor.eq.l) term2 = term2 + 0.5D0
          if (i.eq.l .and. j_tensor.eq.k) term2 = term2 + 0.5D0
          
          c_vol(m,n) = p_tilde * J * Cinv(i,j_tensor) * Cinv(k,l) -
     &                 2.0D0 * p * J * 0.5D0 * 
     &                 (Cinv(i,k)*Cinv(j_tensor,l) + 
     &                  Cinv(i,l)*Cinv(j_tensor,k))
          
C         Deviatoric part: dS_dev/dC
C         S_dev = 2 * J^(-2/3) * W1 * (I - (I1_bar/3)*C^(-1))
          coeff1 = 2.0D0 * J_minus_2_3 * (W1 + I1_bar * W11)
          coeff2 = 2.0D0 * J_minus_2_3 * W11
          
          c_dev(m,n) = 0.0D0
          
C         Terms from dI1_bar/dC
          if (i.eq.j_tensor .and. k.eq.l) then
             c_dev(m,n) = c_dev(m,n) + coeff1 * J_minus_2_3
          endif
          
          if (i.eq.j_tensor) then
             c_dev(m,n) = c_dev(m,n) - 
     &                    coeff1 * J_minus_2_3 * (I1_bar/3.0D0) * Cinv(k,l)
          endif
          if (k.eq.l) then
             c_dev(m,n) = c_dev(m,n) - 
     &                    coeff1 * J_minus_2_3 * (I1_bar/3.0D0) * Cinv(i,j_tensor)
          endif
          
C         Terms from W11 * (dI1_bar/dC) * (dI1_bar/dC)
          c_dev(m,n) = c_dev(m,n) + 
     &                 coeff2 * J_minus_2_3 * J_minus_2_3 * 
     &                 (I_den(i,j_tensor) - (I1_bar/3.0D0)*Cinv(i,j_tensor)) *
     &                 (I_den(k,l) - (I1_bar/3.0D0)*Cinv(k,l))
          
C         Terms from -W1 * (I1_bar/3) * dC^(-1)/dC
          term2 = 0.0D0
          if (i.eq.k) term2 = term2 - 0.5D0 * Cinv(j_tensor,l)
          if (j_tensor.eq.l) term2 = term2 - 0.5D0 * Cinv(i,k)
          if (i.eq.l) term2 = term2 - 0.5D0 * Cinv(j_tensor,k)
          if (j_tensor.eq.k) term2 = term2 - 0.5D0 * Cinv(i,l)
          
          c_dev(m,n) = c_dev(m,n) - 
     &                 2.0D0 * J_minus_2_3 * W1 * (I1_bar/3.0D0) * term2
          
          DMAT(m,n) = c_vol(m,n) + c_dev(m,n)
          
        ENDDO
      ENDDO

      RETURN
      END

      SUBROUTINE CALC_GREEN_LAGRANGE_STRAIN(F, E_voigt)
C     ------------------------------------------------------------------
C     Calculate Green-Lagrange strain tensor for visualization
C     E = 0.5 * (F^T * F - I)
C     Output in Voigt notation: [E11, E22, E33, 2*E12, 2*E23, 2*E13]
C     ------------------------------------------------------------------
      IMPLICIT NONE
      REAL*8, INTENT(IN) :: F(3,3)
      REAL*8, INTENT(OUT) :: E_voigt(6)
      REAL*8 :: C(3,3), E(3,3)
      INTEGER :: i, j
      
C     Calculate right Cauchy-Green tensor: C = F^T * F
      C = matmul(transpose(F), F)
      
C     Calculate Green-Lagrange strain: E = 0.5 * (C - I)
      E = 0.5D0 * C
      E(1,1) = E(1,1) - 0.5D0
      E(2,2) = E(2,2) - 0.5D0
      E(3,3) = E(3,3) - 0.5D0
      
C     Convert to Voigt notation (engineering shear strain)
      E_voigt(1) = E(1,1)
      E_voigt(2) = E(2,2)
      E_voigt(3) = E(3,3)
      E_voigt(4) = 2.0D0 * E(1,2)  ! Engineering shear strain
      E_voigt(5) = 2.0D0 * E(2,3)
      E_voigt(6) = 2.0D0 * E(1,3)
      
      RETURN
      END

C     ================================================================
C     UNIFIED HOURGLASS SUBROUTINES (Hyper version - works for both)
C     ================================================================

      SUBROUTINE GET_CMTXH(DMAT, FJAC, DETJ, Cmtxh)
      use constants_module
      IMPLICIT NONE
      REAL*8, INTENT(IN) :: DMAT(6,6), FJAC(3,3), DETJ
      REAL*8, INTENT(OUT) :: Cmtxh(6,6)
      REAL*8 :: J0_T(3,3), R(3,3), U_diag_inv(3,3)
      REAL*8 :: hat_J0_inv(3,3), rj
      REAL*8 :: j0, j_bar_0
      
      J0_T = transpose(FJAC)
      call POLAR_DECOMP_FOR_J0HINV(J0_T, R, U_diag_inv)
      hat_J0_inv = matmul(R, U_diag_inv)
      
      j0 = DETJ
      j_bar_0 = 1.0D0 / (U_diag_inv(1,1) * U_diag_inv(2,2) * U_diag_inv(3,3))
      rj = j0 / j_bar_0
      
      call ROT_DMTX(DMAT, hat_J0_inv, rj, Cmtxh)
      Cmtxh = SCALE_C_TILDE * Cmtxh
      
      RETURN
      END

      SUBROUTINE POLAR_DECOMP_FOR_J0HINV(J0_T, R, U_diag_inv)
C     ------------------------------------------------------------------
C     Polar decomposition: J0_T = R * U
C     with robustness protection against singular elements
C     ------------------------------------------------------------------
      IMPLICIT NONE
      REAL*8, INTENT(IN) :: J0_T(3,3)
      REAL*8, INTENT(OUT) :: R(3,3), U_diag_inv(3,3)
      REAL*8 :: j1(3), j2(3), j3(3)
      REAL*8 :: j1_norm, j2_norm, j3_norm
      REAL*8 :: q1(3), q2(3), q3(3), q2_norm, q3_norm
      REAL*8, PARAMETER :: SMALL = 1.0D-20
      INTEGER :: i
      
      do i = 1, 3
          j1(i) = J0_T(1,i)
          j2(i) = J0_T(2,i)
          j3(i) = J0_T(3,i)
      enddo
      
C     Calculate norms with protection against zero
      j1_norm = sqrt(dot_product(j1, j1) + SMALL)
      j2_norm = sqrt(dot_product(j2, j2) + SMALL)
      j3_norm = sqrt(dot_product(j3, j3) + SMALL)
      
C     Gram-Schmidt orthogonalization with robustness checks
      q1 = j1 / j1_norm
      
      q2 = j2 - dot_product(j2, q1) * q1
      q2_norm = sqrt(dot_product(q2, q2) + SMALL)
      q2 = q2 / q2_norm
      
      q3 = j3 - dot_product(j3, q1) * q1 - dot_product(j3, q2) * q2
      q3_norm = sqrt(dot_product(q3, q3) + SMALL)
      q3 = q3 / q3_norm
      
      R(1,:) = q1
      R(2,:) = q2
      R(3,:) = q3
      
C     Inverse of stretch tensor (with protection)
      U_diag_inv = 0.0D0
      U_diag_inv(1,1) = 1.0D0 / j1_norm
      U_diag_inv(2,2) = 1.0D0 / j2_norm
      U_diag_inv(3,3) = 1.0D0 / j3_norm
      
      RETURN
      END

      SUBROUTINE ROT_DMTX(D, J0Inv, rj, D_rotated)
      IMPLICIT NONE
      REAL*8, INTENT(IN) :: D(6,6), J0Inv(3,3), rj
      REAL*8, INTENT(OUT) :: D_rotated(6,6)
      REAL*8 :: J_transform(6,6), temp(6,6)
      REAL*8 :: j11, j12, j13, j21, j22, j23, j31, j32, j33
      
      j11 = J0Inv(1,1); j12 = J0Inv(1,2); j13 = J0Inv(1,3)
      j21 = J0Inv(2,1); j22 = J0Inv(2,2); j23 = J0Inv(2,3)
      j31 = J0Inv(3,1); j32 = J0Inv(3,2); j33 = J0Inv(3,3)
      
      J_transform = 0.0D0
      
      J_transform(1,1) = j11*j11
      J_transform(1,2) = j21*j21
      J_transform(1,3) = j31*j31
      J_transform(1,4) = j11*j21
      J_transform(1,5) = j21*j31
      J_transform(1,6) = j11*j31
      
      J_transform(2,1) = j12*j12
      J_transform(2,2) = j22*j22
      J_transform(2,3) = j32*j32
      J_transform(2,4) = j12*j22
      J_transform(2,5) = j22*j32
      J_transform(2,6) = j12*j32
      
      J_transform(3,1) = j13*j13
      J_transform(3,2) = j23*j23
      J_transform(3,3) = j33*j33
      J_transform(3,4) = j13*j23
      J_transform(3,5) = j23*j33
      J_transform(3,6) = j13*j33
      
      J_transform(4,1) = 2.0D0*j11*j12
      J_transform(4,2) = 2.0D0*j21*j22
      J_transform(4,3) = 2.0D0*j31*j32
      J_transform(4,4) = j11*j22 + j21*j12
      J_transform(4,5) = j21*j32 + j31*j22
      J_transform(4,6) = j11*j32 + j31*j12
      
      J_transform(5,1) = 2.0D0*j12*j13
      J_transform(5,2) = 2.0D0*j22*j23
      J_transform(5,3) = 2.0D0*j32*j33
      J_transform(5,4) = j12*j23 + j22*j13
      J_transform(5,5) = j22*j33 + j32*j23
      J_transform(5,6) = j12*j33 + j32*j13
      
      J_transform(6,1) = 2.0D0*j13*j11
      J_transform(6,2) = 2.0D0*j23*j21
      J_transform(6,3) = 2.0D0*j33*j31
      J_transform(6,4) = j13*j21 + j23*j11
      J_transform(6,5) = j23*j31 + j33*j21
      J_transform(6,6) = j13*j31 + j33*j11
      
      temp = matmul(D, J_transform)
      D_rotated = rj * matmul(transpose(J_transform), temp)
      
      RETURN
      END

      SUBROUTINE CALC_K_MATRICES(C_tilde, VOL, Kmat, K_alpha_u, 
     &                           K_alpha_alpha)
      use constants_module
      implicit none
      real*8, intent(in) :: C_tilde(6,6), VOL
      real*8, intent(out) :: Kmat(4,4,3,3), K_alpha_u(4,6,3)
      real*8, intent(out) :: K_alpha_alpha(6,6)
      real*8 :: factor_K123, factor_K4, factor_Kau, H, H43, H51, H62
      real*8 :: C(6,6)
      integer :: i, j
      
      Kmat = 0.0D0
      K_alpha_u = 0.0D0
      K_alpha_alpha = 0.0D0
      C = C_tilde
      
      factor_K123 = (8.0D0/3.0D0) * SCALE_K_MATRIX
      factor_K4 = (8.0D0/9.0D0) * SCALE_K_MATRIX
      factor_Kau = (8.0D0/3.0D0) * SCALE_K_MATRIX
      
      ! K^11
      Kmat(1,1,1,1) = factor_K123 * C(1,1)
      Kmat(1,1,1,3) = factor_K123 * C(1,6)
      Kmat(1,1,2,2) = factor_K123 * C(2,2)
      Kmat(1,1,2,3) = factor_K123 * C(2,5)
      Kmat(1,1,3,1) = factor_K123 * C(6,1)
      Kmat(1,1,3,2) = factor_K123 * C(5,2)
      Kmat(1,1,3,3) = factor_K123 * (C(5,5) + C(6,6))
      
      ! K^22
      Kmat(2,2,1,1) = factor_K123 * C(1,1)
      Kmat(2,2,1,2) = factor_K123 * C(1,4)
      Kmat(2,2,2,1) = factor_K123 * C(4,1)
      Kmat(2,2,2,2) = factor_K123 * (C(5,5) + C(4,4))
      Kmat(2,2,2,3) = factor_K123 * C(5,3)
      Kmat(2,2,3,2) = factor_K123 * C(3,5)
      Kmat(2,2,3,3) = factor_K123 * C(3,3)
      
      ! K^33
      Kmat(3,3,1,1) = factor_K123 * (C(6,6) + C(4,4))
      Kmat(3,3,1,2) = factor_K123 * C(4,2)
      Kmat(3,3,1,3) = factor_K123 * C(6,3)
      Kmat(3,3,2,1) = factor_K123 * C(2,4)
      Kmat(3,3,2,2) = factor_K123 * C(2,2)
      Kmat(3,3,3,1) = factor_K123 * C(3,6)
      Kmat(3,3,3,3) = factor_K123 * C(3,3)
      
      ! K^44
      Kmat(4,4,1,1) = factor_K4 * C(1,1)
      Kmat(4,4,2,2) = factor_K4 * C(2,2)
      Kmat(4,4,3,3) = factor_K4 * C(3,3)
      
      ! Cross terms
      Kmat(1,2,2,2) = factor_K123 * C(2,5)
      Kmat(1,2,2,3) = factor_K123 * C(2,3)
      Kmat(1,2,3,2) = factor_K123 * C(5,5)
      Kmat(1,2,3,3) = factor_K123 * C(5,3)
      
      Kmat(1,3,1,1) = factor_K123 * C(1,6)
      Kmat(1,3,1,3) = factor_K123 * C(1,3)
      Kmat(1,3,3,1) = factor_K123 * C(6,6)
      Kmat(1,3,3,3) = factor_K123 * C(6,3)
      
      Kmat(2,1,2,2) = factor_K123 * C(5,2)
      Kmat(2,1,2,3) = factor_K123 * C(5,5)
      Kmat(2,1,3,2) = factor_K123 * C(3,2)
      Kmat(2,1,3,3) = factor_K123 * C(3,5)
      
      Kmat(2,3,1,1) = factor_K123 * C(1,4)
      Kmat(2,3,1,2) = factor_K123 * C(1,2)
      Kmat(2,3,2,1) = factor_K123 * C(4,4)
      Kmat(2,3,2,2) = factor_K123 * C(4,2)
      
      Kmat(3,1,1,1) = factor_K123 * C(6,1)
      Kmat(3,1,1,3) = factor_K123 * C(6,6)
      Kmat(3,1,3,1) = factor_K123 * C(3,1)
      Kmat(3,1,3,3) = factor_K123 * C(3,6)
      
      Kmat(3,2,1,1) = factor_K123 * C(4,1)
      Kmat(3,2,1,2) = factor_K123 * C(4,4)
      Kmat(3,2,2,1) = factor_K123 * C(2,1)
      Kmat(3,2,2,2) = factor_K123 * C(2,4)
      
      ! K_alpha_u
      K_alpha_u(1,1,2) = factor_Kau*C(1,2)
      K_alpha_u(1,1,3) = factor_Kau*C(1,5)
      K_alpha_u(1,2,1) = factor_Kau*C(2,1)
      K_alpha_u(1,2,3) = factor_Kau*C(2,6)
      
      K_alpha_u(2,1,2) = factor_Kau*C(1,5)
      K_alpha_u(2,1,3) = factor_Kau*C(1,3)
      K_alpha_u(2,3,1) = factor_Kau*C(3,1)
      K_alpha_u(2,3,2) = factor_Kau*C(3,4)
      
      K_alpha_u(3,2,1) = factor_Kau*C(2,6)
      K_alpha_u(3,2,3) = factor_Kau*C(2,3)
      K_alpha_u(3,3,1) = factor_Kau*C(3,4)
      K_alpha_u(3,3,2) = factor_Kau*C(3,2)
      
      H43 = C(1,3) + C(2,3) + C(3,3)
      H51 = C(1,1) + C(2,1) + C(3,1)
      H62 = C(1,2) + C(2,2) + C(3,2)
      K_alpha_u(4,4,3) = factor_K4*H43
      K_alpha_u(4,5,1) = factor_K4*H51
      K_alpha_u(4,6,2) = factor_K4*H62
      
      ! K_alpha_alpha
      H = C(1,1) + C(2,2) + C(3,3) + 
     &    2.0D0*(C(1,2) + C(2,3) + C(1,3))
      
      K_alpha_alpha(1,1) = factor_Kau*C(1,1)
      K_alpha_alpha(2,2) = factor_Kau*C(2,2)
      K_alpha_alpha(3,3) = factor_Kau*C(3,3)
      K_alpha_alpha(4,4) = factor_Kau*H/3.0D0
      K_alpha_alpha(5,5) = factor_Kau*H/3.0D0
      K_alpha_alpha(6,6) = factor_Kau*H/3.0D0
      
      RETURN
      END

      SUBROUTINE INVERT_6X6_FULL(A, AINV)
      IMPLICIT NONE
      REAL*8, INTENT(IN)  :: A(6,6)
      REAL*8, INTENT(OUT) :: AINV(6,6)
      REAL*8 :: WORK(6,6), PIVOT, FACTOR, TEMP
      INTEGER :: I, J, K, P
      
      DO I = 1, 6
          DO J = 1, 6
              WORK(I,J) = A(I,J)
              IF (I .EQ. J) THEN
                  AINV(I,J) = 1.0D0
              ELSE
                  AINV(I,J) = 0.0D0
              ENDIF
          ENDDO
      ENDDO
      
      DO K = 1, 6
          PIVOT = ABS(WORK(K,K))
          P = K
          DO I = K+1, 6
              IF (ABS(WORK(I,K)) .GT. PIVOT) THEN
                  PIVOT = ABS(WORK(I,K))
                  P = I
              ENDIF
          ENDDO
          
          IF (PIVOT .LT. 1.0D-20) THEN
              WRITE(*,*) 'ERROR: Singular Matrix in INVERT_6X6_FULL'
              AINV = 0.0D0 
              RETURN
          ENDIF
          
          IF (P .NE. K) THEN
              DO J = 1, 6
                  TEMP = WORK(P,J)
                  WORK(P,J) = WORK(K,J)
                  WORK(K,J) = TEMP
                  
                  TEMP = AINV(P,J)
                  AINV(P,J) = AINV(K,J)
                  AINV(K,J) = TEMP
              ENDDO
          ENDIF
          
          FACTOR = 1.0D0 / WORK(K,K)
          DO J = 1, 6
              WORK(K,J) = WORK(K,J) * FACTOR
              AINV(K,J) = AINV(K,J) * FACTOR
          ENDDO
          
          DO I = 1, 6
              IF (I .NE. K) THEN
                  FACTOR = WORK(I,K)
                  DO J = 1, 6
                      WORK(I,J) = WORK(I,J) - FACTOR * WORK(K,J)
                      AINV(I,J) = AINV(I,J) - FACTOR * AINV(K,J)
                  ENDDO
              ENDIF
          ENDDO
      ENDDO
      
      RETURN
      END

      SUBROUTINE INVERT_6X6_DIAGONAL(A, AINV)
C     ------------------------------------------------------------------
C     Diagonal-only inversion for K_alpha_alpha matrix
C     Faster but less accurate for coupled materials (nu != 0)
C     ------------------------------------------------------------------
      IMPLICIT NONE
      REAL*8, INTENT(IN)  :: A(6,6)
      REAL*8, INTENT(OUT) :: AINV(6,6)
      INTEGER :: I, J
      REAL*8, PARAMETER :: SMALL = 1.0D-20
      
      AINV = 0.0D0
      
      DO I = 1, 6
          IF (ABS(A(I,I)) .LT. SMALL) THEN
              WRITE(*,*) 'ERROR: Near-zero diagonal element in INVERT_6X6_DIAGONAL'
              WRITE(*,*) 'Element (', I, ',', I, ') = ', A(I,I)
              AINV = 0.0D0
              RETURN
          ENDIF
          AINV(I,I) = 1.0D0 / A(I,I)
      ENDDO
      
      RETURN
      END

      SUBROUTINE CALC_DF_HG_EAS(Kmat, K_alpha_u, K_alpha_alpha_inv,
     &                          gammas, delta_u, J0_T, df_hg)
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
          K_i = 0.0D0
          
          do j = 1, 4
              Gamma_j_T = 0.0D0
              do node = 1, 8
                  Gamma_j_T(1, node*3-2) = gammas(node, j)
                  Gamma_j_T(2, node*3-1) = gammas(node, j)
                  Gamma_j_T(3, node*3-0) = gammas(node, j)
              enddo
              
              K_au_i_T = transpose(K_alpha_u(i,:,:))
              K_aa_inv_K_au_j = matmul(K_alpha_alpha_inv,  
     &                                  K_alpha_u(j,:,:))
              temp33 = matmul(K_au_i_T, K_aa_inv_K_au_j)
              K_ij_condensed = Kmat(i,j,:,:) - temp33
              
              J0T_Gamma_j_T = matmul(J0_T, Gamma_j_T)
              K_i = K_i + matmul(K_ij_condensed, J0T_Gamma_j_T)
          enddo
          
          df_hg(:,i) = matmul(K_i, delta_u)
      enddo
      
      RETURN
      END

